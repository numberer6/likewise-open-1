#ifndef __SRV_FILE_H__
#define __SRV_FILE_H__

NTSTATUS
SrvFileCreate(
    USHORT                  fid,
    PWSTR                   pwszFilename,
    PIO_FILE_HANDLE         phFile,
    PIO_FILE_NAME*          ppFilename,
    ACCESS_MASK             desiredAccess,
    LONG64                  allocationSize,
    FILE_ATTRIBUTES         fileAttributes,
    FILE_SHARE_FLAGS        shareAccess,
    FILE_CREATE_DISPOSITION createDisposition,
    FILE_CREATE_OPTIONS     createOptions,
    PLWIO_SRV_FILE*          ppFile
    );

VOID
SrvFileRelease(
    PLWIO_SRV_FILE pFile
    );

#endif /* __SRV_FILE_H__ */
