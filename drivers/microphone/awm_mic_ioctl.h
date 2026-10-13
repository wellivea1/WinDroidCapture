#ifndef AWM_MIC_IOCTL_H
#define AWM_MIC_IOCTL_H
#define AWM_MIC_VERSION 2u
#define AWM_MIC_RATE 48000u
#define AWM_MIC_CHANNELS 1u
#define AWM_MIC_BITS 16u
#define AWM_MIC_MAX_WRITE 9600u
#define IOCTL_AWM_MIC_BEGIN CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_BUFFERED,FILE_WRITE_DATA)
#define IOCTL_AWM_MIC_WRITE CTL_CODE(FILE_DEVICE_UNKNOWN,0x801,METHOD_IN_DIRECT,FILE_WRITE_DATA)
#define IOCTL_AWM_MIC_RESET CTL_CODE(FILE_DEVICE_UNKNOWN,0x802,METHOD_BUFFERED,FILE_WRITE_DATA)
#define IOCTL_AWM_MIC_QUERY CTL_CODE(FILE_DEVICE_UNKNOWN,0x803,METHOD_BUFFERED,FILE_READ_DATA)
typedef struct AwmMicBegin { unsigned int version,generation,sample_rate,channels,bits; } AwmMicBegin;
typedef struct AwmMicWrite { unsigned int version,generation,samples,reserved; long long pts_us; } AwmMicWrite;
typedef struct AwmMicStats { unsigned int version,generation,queued_samples,streams; unsigned long long samples_written,samples_delivered,underruns,overruns; } AwmMicStats;
#endif
