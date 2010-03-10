#ifndef __STRUCTS_H__
#define __STRUCTS_H__

// Owned by listener
typedef struct _SRV_TRANSPORT_LISTENER {
    SRV_TRANSPORT_HANDLE pTransport;
    PLW_THREAD_POOL pPool;
    PLW_TASK pTask;
    PLW_TASK_GROUP pTaskGroup;
    int ListenFd;
} SRV_TRANSPORT_LISTENER, *PSRV_TRANSPORT_LISTENER;

// Top-level structure
typedef struct _SRV_TRANSPORT_HANDLE_DATA {
    SRV_TRANSPORT_PROTOCOL_DISPATCH Dispatch;
    PSRV_PROTOCOL_TRANSPORT_CONTEXT pContext;
    SRV_TRANSPORT_LISTENER Listener;
} SRV_TRANSPORT_HANDLE_DATA, *PSRV_TRANSPORT_HANDLE_DATA;

typedef ULONG SRV_SOCKET_STATE_MASK, *PSRV_SOCKET_STATE_MASK;

#define SRV_SOCKET_STATE_EXTERNAL_CLOSE 0x00000001
#define SRV_SOCKET_STATE_FD_WRITABLE    0x00000002
#define SRV_SOCKET_STATE_FD_READABLE    0x00000004
#define SRV_SOCKET_STATE_CLOSED         0x00000008

// Abstraction for socket address to handle different address types.
typedef union {
    struct sockaddr Generic;
    struct sockaddr_in Ip;
#ifdef AF_INET6
    struct sockaddr_in6 Ip6;
#endif
} SRV_SOCKET_ADDRESS, *PSRV_SOCKET_ADDRESS;

// Transport abstraction for a connection.
typedef struct _SRV_SOCKET
{
    LW_RTL_MUTEX Mutex;

    // Back reference.
    PSRV_TRANSPORT_LISTENER pListener;

    // Protocol connection context.
    PSRV_CONNECTION pConnection;

    // Socket-specific information.

    // Immutable for life of the task.
    int fd;
    SRV_SOCKET_ADDRESS ClientAddress;
    SOCKLEN_T ClientAddressLength;
    CHAR AddresssStringBuffer[SRV_SOCKET_ADDRESS_STRING_MAX_SIZE];

    PLW_TASK pTask;
    SRV_SOCKET_STATE_MASK StateMask;
    NTSTATUS DoneStatus;
    // Buffer information
    PVOID pBuffer;
    ULONG Size;
    ULONG Minimum;
    ULONG Offset;
    // Send queue - (SRV_SEND_ITEM.SendLinks)
    LW_LIST_LINKS SendHead;
} SRV_SOCKET, *PLWIO_SRV_SOCKET;

typedef struct _SRV_SEND_ITEM
{
    LW_LIST_LINKS SendLinks;
    PSRV_SEND_CONTEXT pSendContext;
    PIO_ZCT pZct;
    PVOID pBuffer;
    ULONG Length;
    ULONG Offset;
} SRV_SEND_ITEM, *PSRV_SEND_ITEM;

typedef struct _LWIO_SRV_LISTENER_CONTEXT
{
    pthread_mutex_t  mutex;
    pthread_mutex_t* pMutex;

    PLW_THREAD_POOL pPool;
    PLW_TASK pTask;
    PLW_TASK_GROUP pTaskGroup;
    int listenFd;

    SRV_PROPERTIES serverProperties;

    // Invariant
    // Not owned
    HANDLE                     hPacketAllocator;
    HANDLE                     hGssContext;
    PLWIO_SRV_SHARE_ENTRY_LIST pShareList;
} LWIO_SRV_LISTENER_CONTEXT, *PLWIO_SRV_LISTENER_CONTEXT;

typedef struct _LWIO_SRV_LISTENER
{
    LWIO_SRV_LISTENER_CONTEXT context;

} LWIO_SRV_LISTENER, *PLWIO_SRV_LISTENER;

typedef struct _LWIO_SRV_THREADPOOL_TRANSPORT_CONFIG
{
    BOOLEAN bEnableSigning;
    BOOLEAN bRequireSigning;

} LWIO_SRV_THREADPOOL_TRANSPORT_CONFIG, *PLWIO_SRV_THREADPOOL_TRANSPORT_CONFIG;

typedef struct _LWIO_SRV_THREADPOOL_TRANSPORT_GLOBALS
{
    pthread_mutex_t              mutex;

    PSMB_PROD_CONS_QUEUE         pWorkQueue;

    LWIO_SRV_LISTENER            listener;

    PLWIO_PACKET_ALLOCATOR       hPacketAllocator;

    PLWIO_SRV_SHARE_ENTRY_LIST   pShareList;

} LWIO_SRV_THREADPOOL_TRANSPORT_GLOBALS, *PLWIO_SRV_THREADPOOL_TRANSPORT_GLOBALS;

#endif /* __STRUCTS_H__ */
