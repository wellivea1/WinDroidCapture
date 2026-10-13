/* C WDM PortCls/WaveRT virtual microphone. No Android or codec code in kernel. */
#include <wdm.h>
#include <initguid.h>
#include <punknown.h>
#include <windef.h>
#define NOBITMAP
#include <mmreg.h>
#undef NOBITMAP
#include <ks.h>
#include <ksmedia.h>
#include <drmk.h>
#define INTERFACE IUnknown
#include <portcls.h>
#include <wdmsec.h>
#include "awm_mic_ioctl.h"
#include "mic_ring.h"
#define TAG 'mWAA'
DEFINE_GUID(AWM_CONTROL_CLASS,0xb199c5fd,0x62ae,0x48b9,0xa1,0xcb,0xf8,0x7a,0x6d,0x1b,0xa0,0x03);
DEFINE_GUID(AWM_MIC_NAME,0xd3f875c1,0x9354,0x4b10,0x90,0x01,0x99,0x5d,0xb5,0xcd,0xcb,0x78);
typedef struct Mini {const void *vtable;LONG refs;BOOLEAN topology;PPORTWAVERT port;} Mini;
typedef struct Stream {
    IMiniportWaveRTStreamNotification iface;IMiniportWaveRTInputStream input;
    LONG refs;PPORTWAVERTSTREAM port;PMDL mdl;BYTE *buffer;ULONG bytes,notifications;
    KSSTATE state;KSPIN_LOCK lock;KTIMER timer;KDPC dpc;PKEVENT events[4];
    ULONG64 position,last_time,packet_number,last_read_packet;LARGE_INTEGER frequency;BOOLEAN running;
} Stream;
static struct {PDEVICE_OBJECT control;UNICODE_STRING link;PFILE_OBJECT owner;KSPIN_LOCK lock;AwmMicRing ring;LONG streams;PDRIVER_UNLOAD port_unload;} device;
static BOOLEAN same(REFGUID a,REFGUID b){return RtlCompareMemory(a,b,sizeof(GUID))==sizeof(GUID);}
static ULONGLONG milliseconds(void){return KeQueryInterruptTime()/10000;}
static NTSTATUS finish(PIRP irp,NTSTATUS status,ULONG_PTR bytes){irp->IoStatus.Status=status;irp->IoStatus.Information=bytes;IoCompleteRequest(irp,IO_NO_INCREMENT);return status;}
static NTSTATUS dispatch(PDEVICE_OBJECT object,PIRP irp){
    if(object!=device.control)return PcDispatchIrp(object,irp);
    PIO_STACK_LOCATION stack=IoGetCurrentIrpStackLocation(irp);NTSTATUS status=STATUS_INVALID_DEVICE_REQUEST;ULONG_PTR result=0;KIRQL level;
    if(stack->MajorFunction==IRP_MJ_CREATE)return finish(irp,STATUS_SUCCESS,0);
    if(stack->MajorFunction==IRP_MJ_CLOSE||stack->MajorFunction==IRP_MJ_CLEANUP){KeAcquireSpinLock(&device.lock,&level);if(device.owner==stack->FileObject){device.owner=NULL;awm_ring_reset(&device.ring);}KeReleaseSpinLock(&device.lock,level);return finish(irp,STATUS_SUCCESS,0);}
    if(stack->MajorFunction!=IRP_MJ_DEVICE_CONTROL)return finish(irp,status,0);
    ULONG code=stack->Parameters.DeviceIoControl.IoControlCode,in=stack->Parameters.DeviceIoControl.InputBufferLength,out=stack->Parameters.DeviceIoControl.OutputBufferLength;
    if(code==IOCTL_AWM_MIC_QUERY&&out>=sizeof(AwmMicStats)){AwmMicStats *s=irp->AssociatedIrp.SystemBuffer;KeAcquireSpinLock(&device.lock,&level);*s=(AwmMicStats){AWM_MIC_VERSION,device.ring.generation,device.ring.count,(unsigned)device.streams,device.ring.written,device.ring.delivered,device.ring.underruns,device.ring.overruns};KeReleaseSpinLock(&device.lock,level);return finish(irp,STATUS_SUCCESS,sizeof(*s));}
    if((code==IOCTL_AWM_MIC_BEGIN||code==IOCTL_AWM_MIC_RESET)&&in==sizeof(AwmMicBegin)){
        AwmMicBegin b=*(AwmMicBegin *)irp->AssociatedIrp.SystemBuffer;
        if(b.version!=2||!b.generation||b.sample_rate!=48000||b.channels!=1||b.bits!=16)return finish(irp,STATUS_INVALID_PARAMETER,0);
        KeAcquireSpinLock(&device.lock,&level);
        if(device.owner&&device.owner!=stack->FileObject)status=STATUS_DEVICE_BUSY;
        else if(code==IOCTL_AWM_MIC_BEGIN){device.owner=stack->FileObject;awm_ring_begin(&device.ring,b.generation);status=STATUS_SUCCESS;}
        else if(device.owner==stack->FileObject&&device.ring.generation==b.generation){awm_ring_reset(&device.ring);device.ring.generation=b.generation;device.ring.active=1;status=STATUS_SUCCESS;}
        else status=STATUS_ACCESS_DENIED;KeReleaseSpinLock(&device.lock,level);return finish(irp,status,0);
    }
    if(code==IOCTL_AWM_MIC_WRITE&&in==sizeof(AwmMicWrite)&&irp->MdlAddress){
        AwmMicWrite w=*(AwmMicWrite *)irp->AssociatedIrp.SystemBuffer;
        if(w.version!=2||w.reserved||!w.samples||w.samples>AWM_MIC_MAX_WRITE||out!=w.samples*2)return finish(irp,STATUS_INVALID_PARAMETER,0);
        const SHORT *p=MmGetSystemAddressForMdlSafe(irp->MdlAddress,NormalPagePriority|MdlMappingNoExecute|MdlMappingNoWrite);if(!p)return finish(irp,STATUS_INSUFFICIENT_RESOURCES,0);
        KeAcquireSpinLock(&device.lock,&level);
        status=device.owner==stack->FileObject&&awm_ring_write(&device.ring,w.generation,p,w.samples,w.pts_us,milliseconds())?STATUS_SUCCESS:STATUS_ACCESS_DENIED;
        KeReleaseSpinLock(&device.lock,level);return finish(irp,status,result);
    }return finish(irp,STATUS_INVALID_PARAMETER,0);
}
static KSDATARANGE analog={sizeof(KSDATARANGE),0,0,0,STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)};
static KSDATARANGE_AUDIO pcm={{sizeof(KSDATARANGE_AUDIO),0,0,0,STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)},1,16,16,48000,48000};
static PKSDATARANGE analog_ranges[]={&analog},pcm_ranges[]={(PKSDATARANGE)&pcm};
static PCPIN_DESCRIPTOR topo_pins[]={
    {0,0,0,NULL,{0,NULL,0,NULL,1,analog_ranges,KSPIN_DATAFLOW_IN,KSPIN_COMMUNICATION_NONE,&KSNODETYPE_MICROPHONE,&AWM_MIC_NAME,0}},
    {0,0,0,NULL,{0,NULL,0,NULL,1,analog_ranges,KSPIN_DATAFLOW_OUT,KSPIN_COMMUNICATION_NONE,&KSCATEGORY_AUDIO,NULL,0}}
};
static PCCONNECTION_DESCRIPTOR topo_connections[]={{PCFILTER_NODE,0,PCFILTER_NODE,1}};
static PCFILTER_DESCRIPTOR topo_filter={0,NULL,sizeof(PCPIN_DESCRIPTOR),2,topo_pins,sizeof(PCNODE_DESCRIPTOR),0,NULL,1,topo_connections,0,NULL};
static PCPIN_DESCRIPTOR wave_pins[]={
    {0,0,0,NULL,{0,NULL,0,NULL,1,analog_ranges,KSPIN_DATAFLOW_IN,KSPIN_COMMUNICATION_NONE,&KSCATEGORY_AUDIO,NULL,0}},
    {1,1,0,NULL,{0,NULL,0,NULL,1,pcm_ranges,KSPIN_DATAFLOW_OUT,KSPIN_COMMUNICATION_SINK,&KSCATEGORY_AUDIO,&KSAUDFNAME_RECORDING_CONTROL,0}}
};
static PCNODE_DESCRIPTOR wave_nodes[]={{0,NULL,&KSNODETYPE_ADC,NULL}};
static PCCONNECTION_DESCRIPTOR wave_connections[]={{PCFILTER_NODE,0,0,1},{0,0,PCFILTER_NODE,1}};
static PCFILTER_DESCRIPTOR wave_filter={0,NULL,sizeof(PCPIN_DESCRIPTOR),2,wave_pins,sizeof(PCNODE_DESCRIPTOR),1,wave_nodes,2,wave_connections,0,NULL};
static BOOLEAN valid_format(PKSDATAFORMAT data){
    if(!data||data->FormatSize<sizeof(KSDATAFORMAT_WAVEFORMATEX)||!same(&data->MajorFormat,&KSDATAFORMAT_TYPE_AUDIO)||!same(&data->SubFormat,&KSDATAFORMAT_SUBTYPE_PCM)||!same(&data->Specifier,&KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))return FALSE;
    PWAVEFORMATEX w=&((PKSDATAFORMAT_WAVEFORMATEX)data)->WaveFormatEx;
    if(w->nSamplesPerSec!=48000||w->nChannels!=1||w->wBitsPerSample!=16||w->nBlockAlign!=2||w->nAvgBytesPerSec!=96000)return FALSE;
    if(w->wFormatTag==WAVE_FORMAT_PCM)return w->cbSize==0;
    if(w->wFormatTag!=WAVE_FORMAT_EXTENSIBLE||w->cbSize<22||data->FormatSize<sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE))return FALSE;
    PWAVEFORMATEXTENSIBLE extended=&((PKSDATAFORMAT_WAVEFORMATEXTENSIBLE)data)->WaveFormatExt;
    return extended->Samples.wValidBitsPerSample==16&&same(&extended->SubFormat,&KSDATAFORMAT_SUBTYPE_PCM)&&(extended->dwChannelMask==KSAUDIO_SPEAKER_MONO||extended->dwChannelMask==0);
}
static ULONG STDMETHODCALLTYPE mini_add(IUnknown *i){return (ULONG)InterlockedIncrement(&((Mini *)i)->refs);}
static ULONG STDMETHODCALLTYPE mini_release(IUnknown *i){Mini *m=(Mini *)i;LONG n=InterlockedDecrement(&m->refs);if(!n){if(m->port)m->port->lpVtbl->Release((IUnknown *)m->port);ExFreePoolWithTag(m,TAG);}return (ULONG)n;}
static NTSTATUS STDMETHODCALLTYPE mini_query(IUnknown *i,REFIID id,PVOID *out){
    if(!out)return STATUS_INVALID_PARAMETER;*out=NULL;Mini *m=(Mini *)i;
    if(same(id,&IID_IUnknown)||same(id,&IID_IMiniport)||(m->topology?same(id,&IID_IMiniportTopology):same(id,&IID_IMiniportWaveRT))){*out=i;mini_add(i);return STATUS_SUCCESS;}return STATUS_NOINTERFACE;
}
static NTSTATUS STDMETHODCALLTYPE description(IUnknown *i,PPCFILTER_DESCRIPTOR *out){if(!out)return STATUS_INVALID_PARAMETER;*out=((Mini *)i)->topology?&topo_filter:&wave_filter;return STATUS_SUCCESS;}
static NTSTATUS STDMETHODCALLTYPE intersection(IUnknown *i,ULONG pin,PKSDATARANGE a,PKSDATARANGE b,ULONG bytes,PVOID out,PULONG required){
    UNREFERENCED_PARAMETER(i);UNREFERENCED_PARAMETER(b);if(!required||pin!=1||!a||a->FormatSize<sizeof(KSDATARANGE_AUDIO))return STATUS_NO_MATCH;
    PKSDATARANGE_AUDIO r=(PKSDATARANGE_AUDIO)a;if(r->MaximumChannels<1||r->MinimumBitsPerSample>16||r->MaximumBitsPerSample<16||r->MinimumSampleFrequency>48000||r->MaximumSampleFrequency<48000)return STATUS_NO_MATCH;
    *required=sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);if(!bytes)return STATUS_BUFFER_OVERFLOW;if(bytes<*required)return STATUS_BUFFER_TOO_SMALL;if(!out)return STATUS_INVALID_PARAMETER;
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE f={{sizeof(f),0,2,0,STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)},{{WAVE_FORMAT_EXTENSIBLE,1,48000,96000,2,16,22},16,KSAUDIO_SPEAKER_MONO,STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)}};RtlCopyMemory(out,&f,sizeof(f));return STATUS_SUCCESS;
}
static Stream *stream_from(IUnknown *i){return (Stream *)i;}
static ULONG STDMETHODCALLTYPE stream_add(IUnknown *i){return (ULONG)InterlockedIncrement(&stream_from(i)->refs);}
static void stop_timer(Stream *s){s->running=FALSE;KeCancelTimer(&s->timer);KeFlushQueuedDpcs();}
static VOID free_buffer(Stream *s){
    if(s->buffer&&s->mdl)s->port->lpVtbl->UnmapAllocatedPages((IUnknown *)s->port,s->buffer,s->mdl);
    if(s->mdl)s->port->lpVtbl->FreePagesFromMdl((IUnknown *)s->port,s->mdl);s->mdl=NULL;s->buffer=NULL;s->bytes=0;
}
static ULONG STDMETHODCALLTYPE stream_release(IUnknown *i){Stream *s=stream_from(i);LONG n=InterlockedDecrement(&s->refs);if(!n){stop_timer(s);free_buffer(s);s->port->lpVtbl->Release((IUnknown *)s->port);InterlockedDecrement(&device.streams);ExFreePoolWithTag(s,TAG);}return (ULONG)n;}
static NTSTATUS STDMETHODCALLTYPE stream_query(IUnknown *i,REFIID id,PVOID *out){
    if(!out)return STATUS_INVALID_PARAMETER;*out=NULL;Stream *s=stream_from(i);
    if(same(id,&IID_IUnknown)||same(id,&IID_IMiniportWaveRTStream)||same(id,&IID_IMiniportWaveRTStreamNotification))*out=&s->iface;
    else if(same(id,&IID_IMiniportWaveRTInputStream))*out=&s->input;else return STATUS_NOINTERFACE;stream_add(i);return STATUS_SUCCESS;
}
static NTSTATUS STDMETHODCALLTYPE stream_format(IUnknown *i,PKSDATAFORMAT f){Stream *s=stream_from(i);return s->state==KSSTATE_STOP&&valid_format(f)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;}
static VOID timer_dpc(PKDPC dpc,PVOID context,PVOID a,PVOID b){
    UNREFERENCED_PARAMETER(dpc);UNREFERENCED_PARAMETER(a);UNREFERENCED_PARAMETER(b);Stream *s=context;KIRQL level;KeAcquireSpinLock(&s->lock,&level);
    if(s->running&&s->buffer&&s->bytes){ULONGLONG now=milliseconds(),delta=now-s->last_time;if(delta>100)delta=100;s->last_time=now;ULONG samples=(ULONG)delta*48;
        if(samples){ULONG offset=(ULONG)(s->position%s->bytes),remaining=samples;SHORT temp[240];
            while(remaining){ULONG chunk=remaining>240?240:remaining;KIRQL ring_level;KeAcquireSpinLock(&device.lock,&ring_level);awm_ring_read(&device.ring,temp,chunk,now);KeReleaseSpinLock(&device.lock,ring_level);
                for(ULONG k=0;k<chunk;k++){*(SHORT *)(s->buffer+offset)=temp[k];offset=(offset+2)%s->bytes;}remaining-=chunk;}s->position+=(ULONG64)samples*2;
            ULONG packet_size=s->notifications?s->bytes/s->notifications:s->bytes;ULONG64 packet=s->position/packet_size;if(packet>s->packet_number){s->packet_number=packet;for(ULONG k=0;k<4;k++)if(s->events[k])KeSetEvent(s->events[k],IO_NO_INCREMENT,FALSE);}
        }
    }KeReleaseSpinLock(&s->lock,level);
}
static NTSTATUS STDMETHODCALLTYPE stream_state(IUnknown *i,KSSTATE state){
    Stream *s=stream_from(i);if(state<KSSTATE_STOP||state>KSSTATE_RUN)return STATUS_INVALID_PARAMETER;if(state==KSSTATE_RUN&&!s->buffer)return STATUS_INVALID_DEVICE_STATE;
    if(state!=KSSTATE_RUN)stop_timer(s);KIRQL level;KeAcquireSpinLock(&s->lock,&level);s->state=state;if(state==KSSTATE_STOP){s->position=s->packet_number=s->last_read_packet=0;if(s->buffer)RtlZeroMemory(s->buffer,s->bytes);}
    if(state==KSSTATE_RUN){s->last_time=milliseconds();s->running=TRUE;LARGE_INTEGER due;due.QuadPart=-50000;KeSetTimerEx(&s->timer,due,5,&s->dpc);}KeReleaseSpinLock(&s->lock,level);return STATUS_SUCCESS;
}
static NTSTATUS STDMETHODCALLTYPE stream_position(IUnknown *i,PKSAUDIO_POSITION position){if(!position)return STATUS_INVALID_PARAMETER;Stream *s=stream_from(i);KIRQL level;KeAcquireSpinLock(&s->lock,&level);position->PlayOffset=s->bytes?s->position%s->bytes:0;position->WriteOffset=position->PlayOffset;KeReleaseSpinLock(&s->lock,level);return STATUS_SUCCESS;}
static NTSTATUS STDMETHODCALLTYPE allocate_buffer(IUnknown *i,ULONG request,PMDL *mdl,PULONG actual,PULONG offset,MEMORY_CACHING_TYPE *cache){
    Stream *s=stream_from(i);if(!mdl||!actual||!offset||!cache||s->mdl||request<1920||request>192000||request%2)return STATUS_INVALID_PARAMETER;
    PHYSICAL_ADDRESS high;high.QuadPart=MAXLONGLONG;s->mdl=s->port->lpVtbl->AllocatePagesForMdl((IUnknown *)s->port,high,request);
    if(!s->mdl)return STATUS_INSUFFICIENT_RESOURCES;s->buffer=s->port->lpVtbl->MapAllocatedPages((IUnknown *)s->port,s->mdl,MmCached);if(!s->buffer){s->port->lpVtbl->FreePagesFromMdl((IUnknown *)s->port,s->mdl);s->mdl=NULL;return STATUS_INSUFFICIENT_RESOURCES;}
    s->bytes=request;RtlZeroMemory(s->buffer,request);*mdl=s->mdl;*actual=request;*offset=0;*cache=MmCached;return STATUS_SUCCESS;
}
static VOID STDMETHODCALLTYPE release_buffer(IUnknown *i,PMDL mdl,ULONG size){UNREFERENCED_PARAMETER(mdl);UNREFERENCED_PARAMETER(size);Stream *s=stream_from(i);stop_timer(s);free_buffer(s);}
static VOID STDMETHODCALLTYPE latency(IUnknown *i,PKSRTAUDIO_HWLATENCY l){UNREFERENCED_PARAMETER(i);if(l){l->FifoSize=0;l->ChipsetDelay=0;l->CodecDelay=0;}}
static NTSTATUS STDMETHODCALLTYPE no_register(IUnknown *i,PKSRTAUDIO_HWREGISTER r){UNREFERENCED_PARAMETER(i);UNREFERENCED_PARAMETER(r);return STATUS_NOT_SUPPORTED;}
static NTSTATUS STDMETHODCALLTYPE allocate_notify(IUnknown *i,ULONG count,ULONG request,PMDL *m,PULONG actual,PULONG offset,MEMORY_CACHING_TYPE *cache){if(count!=2||request%count)return STATUS_INVALID_PARAMETER;Stream *s=stream_from(i);NTSTATUS status=allocate_buffer(i,request,m,actual,offset,cache);if(NT_SUCCESS(status))s->notifications=count;return status;}
static NTSTATUS STDMETHODCALLTYPE register_event(IUnknown *i,PKEVENT event){if(!event)return STATUS_INVALID_PARAMETER;Stream *s=stream_from(i);KIRQL l;KeAcquireSpinLock(&s->lock,&l);NTSTATUS st=STATUS_INSUFFICIENT_RESOURCES;for(int k=0;k<4;k++){if(s->events[k]==event){st=STATUS_SUCCESS;break;}if(!s->events[k]){s->events[k]=event;st=STATUS_SUCCESS;break;}}KeReleaseSpinLock(&s->lock,l);return st;}
static NTSTATUS STDMETHODCALLTYPE unregister_event(IUnknown *i,PKEVENT event){Stream *s=stream_from(i);KIRQL l;KeAcquireSpinLock(&s->lock,&l);NTSTATUS st=STATUS_NOT_FOUND;for(int k=0;k<4;k++)if(s->events[k]==event){s->events[k]=NULL;st=STATUS_SUCCESS;}KeReleaseSpinLock(&s->lock,l);return st;}
static Stream *input_from(IUnknown *i){return CONTAINING_RECORD((IMiniportWaveRTInputStream *)i,Stream,input);}
static NTSTATUS STDMETHODCALLTYPE input_query(IUnknown *i,REFIID id,PVOID *out){return stream_query((IUnknown *)input_from(i),id,out);}
static ULONG STDMETHODCALLTYPE input_add(IUnknown *i){return stream_add((IUnknown *)input_from(i));}
static ULONG STDMETHODCALLTYPE input_release(IUnknown *i){return stream_release((IUnknown *)input_from(i));}
static NTSTATUS STDMETHODCALLTYPE read_packet(IUnknown *i,PULONG packet,PDWORD flags,PULONG64 counter,PBOOL more){
    Stream *s=input_from(i);if(!packet||!flags||!counter||!more)return STATUS_INVALID_PARAMETER;KIRQL l;KeAcquireSpinLock(&s->lock,&l);
    if(s->state!=KSSTATE_RUN||s->packet_number==s->last_read_packet){KeReleaseSpinLock(&s->lock,l);return STATUS_DEVICE_NOT_READY;}
    *packet=(ULONG)(s->packet_number-1);s->last_read_packet=s->packet_number;*flags=0;*counter=(ULONG64)KeQueryPerformanceCounter(NULL).QuadPart;*more=FALSE;KeReleaseSpinLock(&s->lock,l);return STATUS_SUCCESS;
}
static IMiniportWaveRTStreamNotificationVtbl stream_vtable={stream_query,stream_add,stream_release,stream_format,stream_state,stream_position,allocate_buffer,release_buffer,latency,no_register,no_register,allocate_notify,release_buffer,register_event,unregister_event};
static IMiniportWaveRTInputStreamVtbl input_vtable={input_query,input_add,input_release,read_packet};
static NTSTATUS STDMETHODCALLTYPE wave_init(IUnknown *i,PUNKNOWN adapter,PRESOURCELIST resources,PPORTWAVERT port){UNREFERENCED_PARAMETER(adapter);UNREFERENCED_PARAMETER(resources);Mini *m=(Mini *)i;m->port=port;port->lpVtbl->AddRef((IUnknown *)port);return STATUS_SUCCESS;}
static NTSTATUS STDMETHODCALLTYPE new_stream(IUnknown *i,PMINIPORTWAVERTSTREAM *out,PPORTWAVERTSTREAM port,ULONG pin,BOOLEAN capture,PKSDATAFORMAT format){
    UNREFERENCED_PARAMETER(i);if(!out||!capture||pin!=1||!valid_format(format))return STATUS_INVALID_PARAMETER;*out=NULL;if(InterlockedIncrement(&device.streams)!=1){InterlockedDecrement(&device.streams);return STATUS_DEVICE_BUSY;}
    Stream *s=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*s),TAG);if(!s){InterlockedDecrement(&device.streams);return STATUS_INSUFFICIENT_RESOURCES;}
    s->iface.lpVtbl=&stream_vtable;s->input.lpVtbl=&input_vtable;s->refs=1;s->port=port;s->state=KSSTATE_STOP;port->lpVtbl->AddRef((IUnknown *)port);KeInitializeSpinLock(&s->lock);KeInitializeTimer(&s->timer);KeInitializeDpc(&s->dpc,timer_dpc,s);KeQueryPerformanceCounter(&s->frequency);*out=(PMINIPORTWAVERTSTREAM)&s->iface;return STATUS_SUCCESS;
}
static NTSTATUS STDMETHODCALLTYPE device_description(IUnknown *i,PDEVICE_DESCRIPTION d){UNREFERENCED_PARAMETER(i);if(!d)return STATUS_INVALID_PARAMETER;RtlZeroMemory(d,sizeof(*d));d->Version=DEVICE_DESCRIPTION_VERSION;d->InterfaceType=Internal;d->Dma32BitAddresses=TRUE;return STATUS_SUCCESS;}
static NTSTATUS STDMETHODCALLTYPE topo_init(IUnknown *i,PUNKNOWN adapter,PRESOURCELIST r,PPORTTOPOLOGY port){UNREFERENCED_PARAMETER(i);UNREFERENCED_PARAMETER(adapter);UNREFERENCED_PARAMETER(r);UNREFERENCED_PARAMETER(port);return STATUS_SUCCESS;}
static IMiniportWaveRTVtbl wave_vtable={mini_query,mini_add,mini_release,description,intersection,wave_init,new_stream,device_description};
static IMiniportTopologyVtbl topo_vtable={mini_query,mini_add,mini_release,description,intersection,topo_init};
static NTSTATUS make_subdevice(PDEVICE_OBJECT object,PIRP irp,BOOLEAN topology,PPORT *created){
    PPORT port=NULL;NTSTATUS status=PcNewPort(&port,topology?&CLSID_PortTopology:&CLSID_PortWaveRT);if(!NT_SUCCESS(status))return status;
    Mini *m=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*m),TAG);if(!m){port->lpVtbl->Release((IUnknown *)port);return STATUS_INSUFFICIENT_RESOURCES;}m->vtable=topology?(const void *)&topo_vtable:(const void *)&wave_vtable;m->refs=1;m->topology=topology;
    status=port->lpVtbl->Init((IUnknown *)port,object,irp,(PUNKNOWN)m,NULL,NULL);
    if(NT_SUCCESS(status))status=PcRegisterSubdevice(object,topology?L"Topology":L"Wave",(PUNKNOWN)port);mini_release((IUnknown *)m);if(NT_SUCCESS(status))*created=port;else port->lpVtbl->Release((IUnknown *)port);return status;
}
static NTSTATUS start_device(PDEVICE_OBJECT object,PIRP irp,PRESOURCELIST resources){
    UNREFERENCED_PARAMETER(resources);PPORT topo=NULL,wave=NULL;NTSTATUS status=make_subdevice(object,irp,TRUE,&topo);if(!NT_SUCCESS(status))return status;status=make_subdevice(object,irp,FALSE,&wave);
    if(NT_SUCCESS(status))status=PcRegisterPhysicalConnection(object,(PUNKNOWN)topo,1,(PUNKNOWN)wave,0);if(wave)wave->lpVtbl->Release((IUnknown *)wave);topo->lpVtbl->Release((IUnknown *)topo);return status;
}
static NTSTATUS add_device(PDRIVER_OBJECT driver,PDEVICE_OBJECT pdo){return PcAddAdapterDevice(driver,pdo,start_device,2,0);}
static VOID unload(PDRIVER_OBJECT driver){if(device.control){IoDeleteSymbolicLink(&device.link);IoDeleteDevice(device.control);device.control=NULL;}if(device.port_unload)device.port_unload(driver);}
DRIVER_INITIALIZE DriverEntry;
NTSTATUS DriverEntry(PDRIVER_OBJECT driver,PUNICODE_STRING registry){
    NTSTATUS status=PcInitializeAdapterDriver(driver,registry,add_device);if(!NT_SUCCESS(status))return status;device.port_unload=driver->DriverUnload;driver->DriverUnload=unload;KeInitializeSpinLock(&device.lock);awm_ring_reset(&device.ring);
    UNICODE_STRING name,sddl;RtlInitUnicodeString(&name,L"\\Device\\AWMPhoneMic");RtlInitUnicodeString(&device.link,L"\\DosDevices\\AWMPhoneMic");RtlInitUnicodeString(&sddl,L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)");
    status=IoCreateDeviceSecure(driver,0,&name,FILE_DEVICE_UNKNOWN,FILE_DEVICE_SECURE_OPEN,FALSE,&sddl,&AWM_CONTROL_CLASS,&device.control);if(!NT_SUCCESS(status))return status;
    status=IoCreateSymbolicLink(&device.link,&name);if(!NT_SUCCESS(status)){IoDeleteDevice(device.control);device.control=NULL;return status;}device.control->Flags&=~DO_DEVICE_INITIALIZING;
    driver->MajorFunction[IRP_MJ_CREATE]=dispatch;driver->MajorFunction[IRP_MJ_CLOSE]=dispatch;driver->MajorFunction[IRP_MJ_CLEANUP]=dispatch;driver->MajorFunction[IRP_MJ_DEVICE_CONTROL]=dispatch;return STATUS_SUCCESS;
}
