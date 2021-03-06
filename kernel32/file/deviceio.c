/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            kernel32/file/deviceio.c
 * PURPOSE:         Device I/O Base Client Functionality
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include <k32.h>
//#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
DeviceIoControl(IN HANDLE hDevice,
                IN DWORD dwIoControlCode,
                IN LPVOID lpInBuffer OPTIONAL,
                IN DWORD nInBufferSize OPTIONAL,
                OUT LPVOID lpOutBuffer OPTIONAL,
                IN DWORD nOutBufferSize OPTIONAL,
                OUT LPDWORD lpBytesReturned OPTIONAL,
                IN LPOVERLAPPED lpOverlapped OPTIONAL)
{
    BOOL FsIoCtl;
    NTSTATUS Status;
    PVOID ApcContext;
    IO_STATUS_BLOCK Iosb;

    /* Check what kind of IOCTL to send */
    FsIoCtl = ((dwIoControlCode >> 16) == FILE_DEVICE_FILE_SYSTEM);

    /* CHeck for async */
    if (lpOverlapped != NULL)
    {
        /* Set pending status */
        lpOverlapped->Internal = STATUS_PENDING;


        /* Check if there's an APC context */
        ApcContext = (((ULONG_PTR)lpOverlapped->hEvent & 0x1) ? NULL : lpOverlapped);


        /* Send file system control? */
        if (FsIoCtl)
        {
            /* Send it */
            Status = NtFsControlFile(hDevice,
                                     lpOverlapped->hEvent,
                                     NULL,
                                     ApcContext,
                                     (PIO_STATUS_BLOCK)lpOverlapped,
                                     dwIoControlCode,
                                     lpInBuffer,
                                     nInBufferSize,
                                     lpOutBuffer,
                                     nOutBufferSize);
        }
        else
        {
            /* Otherwise send a device control */
            Status = NtDeviceIoControlFile(hDevice,
                                           lpOverlapped->hEvent,
                                           NULL,
                                           ApcContext,
                                           (PIO_STATUS_BLOCK)lpOverlapped,
                                           dwIoControlCode,
                                           lpInBuffer,
                                           nInBufferSize,
                                           lpOutBuffer,
                                           nOutBufferSize);
        }

        /* Check for or information instead of failure */
        if (!(NT_ERROR(Status)) && (lpBytesReturned))
        {
            /* Protect with SEH */
            _SEH2_TRY
            {
                /* Return the bytes */
                *lpBytesReturned = lpOverlapped->InternalHigh;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                /* Return zero bytes */
                *lpBytesReturned = 0;
            }
            _SEH2_END;
        }

        /* Now check for any kind of failure except pending*/
        if (!(NT_SUCCESS(Status)) || (Status == STATUS_PENDING))
        {
            /* Fail */
            SetLastErrorByStatus(Status);
            return FALSE;
        }
    }
    else
    {
        /* Sync case -- send file system code? */
        if (FsIoCtl)
        {
            /* Do it */
            Status = NtFsControlFile(hDevice,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &Iosb,
                                     dwIoControlCode,
                                     lpInBuffer,
                                     nInBufferSize,
                                     lpOutBuffer,
                                     nOutBufferSize);
        }
        else
        {
            /* Send device code instead */
            Status = NtDeviceIoControlFile(hDevice,
                                           NULL,
                                           NULL,
                                           NULL,
                                           &Iosb,
                                           dwIoControlCode,
                                           lpInBuffer,
                                           nInBufferSize,
                                           lpOutBuffer,
                                           nOutBufferSize);
        }

        /* Now check if the operation isn't done yet */
        if (Status == STATUS_PENDING)
        {
            /* Wait for it and get the final status */
            Status = NtWaitForSingleObject(hDevice, FALSE, NULL);
            if (NT_SUCCESS(Status)) Status = Iosb.Status;
        }

        /* Check for success */
        if (NT_SUCCESS(Status))
        {
            /* Return the byte count */
            *lpBytesReturned = Iosb.Information;
        }
        else
        {
            /* Check for informational or warning failure */
            if (!NT_ERROR(Status)) *lpBytesReturned = Iosb.Information;

            /* Return a failure */
            SetLastErrorByStatus(Status);
            return FALSE;
        }
    }

    /* Return success */
    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetOverlappedResult(IN HANDLE hFile,
                    IN LPOVERLAPPED lpOverlapped,
                    OUT LPDWORD lpNumberOfBytesTransferred,
                    IN BOOL bWait)
{
    DWORD WaitStatus;
    HANDLE hObject;


    /* Check for pending operation */
    if (lpOverlapped->Internal == STATUS_PENDING)
    {
        /* Check if the caller is okay with waiting */
        if (!bWait)
        {
            /* Set timeout */
            WaitStatus = WAIT_TIMEOUT;
        }
        else
        {
            /* Wait for the result */
            hObject = lpOverlapped->hEvent ? lpOverlapped->hEvent : hFile;
            WaitStatus = WaitForSingleObject(hObject, INFINITE);
        }


        /* Check for timeout */
        if (WaitStatus == WAIT_TIMEOUT)
        {
            /* We have to override the last error with INCOMPLETE instead */
            SetLastError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }


        /* Fail if we had an error -- the last error is already set */
        if (WaitStatus != 0) return FALSE;
    }


    /* Return bytes transferred */
    *lpNumberOfBytesTransferred = lpOverlapped->InternalHigh;


    /* Check for failure during I/O */
    if (!NT_SUCCESS(lpOverlapped->Internal))
    {
        /* Set the error and fail */
        SetLastErrorByStatus(lpOverlapped->Internal);
        return FALSE;
    }


    /* All done */
    return TRUE;
}

/* EOF */

