/*
 * Copyright (c) Likewise Software.  All rights Reserved.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the license, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.  You should have received a copy
 * of the GNU Lesser General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * LIKEWISE SOFTWARE MAKES THIS SOFTWARE AVAILABLE UNDER OTHER LICENSING
 * TERMS AS WELL.  IF YOU HAVE ENTERED INTO A SEPARATE LICENSE AGREEMENT
 * WITH LIKEWISE SOFTWARE, THEN YOU MAY ELECT TO USE THE SOFTWARE UNDER THE
 * TERMS OF THAT SOFTWARE LICENSE AGREEMENT INSTEAD OF THE TERMS OF THE GNU
 * LESSER GENERAL PUBLIC LICENSE, NOTWITHSTANDING THE ABOVE NOTICE.  IF YOU
 * HAVE QUESTIONS, OR WISH TO REQUEST A COPY OF THE ALTERNATE LICENSING
 * TERMS OFFERED BY LIKEWISE SOFTWARE, PLEASE CONTACT LIKEWISE SOFTWARE AT
 * license@likewisesoftware.com
 */

/*
 * Module Name:
 *
 *        session-default.c
 *
 * Abstract:
 *
 *        Session management API
 *        Default session manager implementation
 *
 * Authors: Brian Koropoff (bkoropoff@likewisesoftware.com)
 *
 */
#include "session-private.h"
#include "util-private.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

typedef struct LWMsgSession
{
    /* Remote session identifier */
    LWMsgSessionID rsmid;
    /* Security token of session creator */
    LWMsgSecurityToken* sec_token;
    /* Reference count */
    unsigned int refs;
    /* Pointer to linked list of handles */
    struct HandleEntry* handles;
    /* Links to other sessions in the manager */
    struct LWMsgSession* next, *prev;
    /* User data pointer */
    void* data;
    /* Data pointer cleanup function */
    LWMsgSessionDataCleanupFunction cleanup;
} SessionEntry;

typedef struct HandleEntry
{
    /* Handle type */
    const char* type;
    /* Handle pointer */
    void* pointer;
    /* Handle locality */
    LWMsgHandleLocation locality;
    /* Handle id */
    unsigned long hid;
    /* Handle cleanup function */
    void (*cleanup)(void*);
    /* Links to other handles in the session */
    struct HandleEntry* next, *prev;
} HandleEntry;

typedef struct DefaultPrivate
{
    SessionEntry* sessions;
    unsigned long next_hid;
} DefaultPrivate;

static void
default_free_handle(
    HandleEntry* entry,
    LWMsgBool do_cleanup
    )
{
    if (entry->cleanup && do_cleanup)
    {
        entry->cleanup(entry->pointer);
    }

    if (entry->prev)
    {
        entry->prev->next = entry->next;
    }
    
    if (entry->next)
    {
        entry->next->prev = entry->prev;
    }

    free(entry);
}

static
LWMsgStatus
default_add_handle(
    SessionEntry* session,
    const char* type,
    LWMsgHandleLocation locality,
    void* pointer,
    unsigned long hid,
    void (*cleanup)(void*),
    HandleEntry** out_handle
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;
    HandleEntry* handle = NULL;

    handle = calloc(1, sizeof(*handle));

    if (!handle)
    {
        BAIL_ON_ERROR(status = LWMSG_STATUS_MEMORY);
    }

    handle->type = type;
    handle->pointer = pointer ? pointer : handle;
    handle->cleanup = cleanup;
    handle->hid = hid;
    handle->locality = locality;

    handle->next = session->handles;

    if (session->handles)
    {
        session->handles->prev = handle;
    }

    session->handles = handle;

    *out_handle = handle;

error:

    return status;
}

static
SessionEntry*
default_find_session(
    DefaultPrivate* priv,
    const LWMsgSessionID* rsmid
    )
{
    SessionEntry* entry = NULL;

    for (entry = priv->sessions; entry; entry = entry->next)
    {
        if (!memcmp(rsmid->bytes, entry->rsmid.bytes, sizeof(rsmid->bytes)))
        {
            return entry;
        }
    }

    return NULL;
}

static
void
default_free_session(
    SessionEntry* session
    )
{
    HandleEntry* handle, *next;

    for (handle = session->handles; handle; handle = next)
    {
        next = handle->next;
        default_free_handle(handle, LWMSG_TRUE);
    }

    if (session->sec_token)
    {
        lwmsg_security_token_delete(session->sec_token);
    }

    if (session->cleanup)
    {
        session->cleanup(session->data);
    }

    free(session);
}

static
LWMsgStatus
default_construct(
    LWMsgSessionManager* manager
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;

    return status;
}

static
void
default_destruct(
    LWMsgSessionManager* manager
    )
{
    DefaultPrivate* priv = lwmsg_session_manager_get_private(manager);
    SessionEntry* entry, *next;

    for (entry = priv->sessions; entry; entry = next)
    {
        next = entry->next;

        default_free_session(entry);
    }
}

static
LWMsgStatus
default_enter_session(
    LWMsgSessionManager* manager,
    const LWMsgSessionID* rsmid,
    LWMsgSecurityToken* rtoken,
    LWMsgSession** out_session
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;
    DefaultPrivate* priv = lwmsg_session_manager_get_private(manager);
    SessionEntry* session = default_find_session(priv, rsmid);
    
    if (session)
    {
        if (!session->sec_token || !lwmsg_security_token_can_access(session->sec_token, rtoken))
        {
            BAIL_ON_ERROR(status = LWMSG_STATUS_SECURITY);
        }

        session->refs++;
    }
    else
    {
        session = calloc(1, sizeof(*session));
        if (!session)
        {
            BAIL_ON_ERROR(status = LWMSG_STATUS_MEMORY);
        }

        memcpy(session->rsmid.bytes, rsmid->bytes, sizeof(rsmid->bytes));

        if (rtoken)
        {
            BAIL_ON_ERROR(status = lwmsg_security_token_copy(rtoken, &session->sec_token));
        }
        
        session->refs = 1;
        session->next = priv->sessions;

        if (priv->sessions)
        {
            priv->sessions->prev = session;
        }

        priv->sessions = session;
    }

    *out_session = session;

error:

    return status;
}

static
LWMsgStatus 
default_leave_session(
    LWMsgSessionManager* manager,
    LWMsgSession* session
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;
    DefaultPrivate* priv = lwmsg_session_manager_get_private(manager);
    
    session->refs--;

    if (session->refs == 0)
    {
        if (priv->sessions == session)
        {
            priv->sessions = priv->sessions->next;
        }

        default_free_session(session);
    }

    return status;
}

static
LWMsgStatus
default_register_handle(
    LWMsgSessionManager* manager,
    LWMsgSession* session,
    const char* type,
    void* pointer,
    void (*cleanup)(void*)
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;
    DefaultPrivate* priv = lwmsg_session_manager_get_private(manager);
    HandleEntry* handle = NULL;

    BAIL_ON_ERROR(status = default_add_handle(
                      session,
                      type,
                      LWMSG_HANDLE_LOCAL,
                      pointer,
                      priv->next_hid++,
                      cleanup,
                      &handle));

error:

    return status;
}

static
LWMsgStatus
default_unregister_handle(
    LWMsgSessionManager* manager,
    LWMsgSession* session,
    void* ptr,
    LWMsgBool do_cleanup
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;
    HandleEntry* handle = NULL;

    if (!session)
    {
        BAIL_ON_ERROR(status = LWMSG_STATUS_NOT_FOUND);
    }

    for (handle = session->handles; handle; handle = handle->next)
    {
        if (handle->pointer == ptr)
        {
            if (handle == session->handles)
            {
                session->handles = session->handles->next;
            }

            default_free_handle(handle, do_cleanup);
            goto done;
        }
    }

    BAIL_ON_ERROR(status = LWMSG_STATUS_NOT_FOUND);

done:

    return status;

error:
    
    goto done;
}


static
LWMsgStatus
default_handle_pointer_to_id(
    LWMsgSessionManager* manager,
    LWMsgSession* session,
    const char* type,
    void* pointer,
    LWMsgBool autoreg,
    LWMsgHandleLocation* out_location,
    unsigned long* out_hid
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;
    DefaultPrivate* priv = lwmsg_session_manager_get_private(manager);
    HandleEntry* handle = NULL;

    if (!session)
    {
        BAIL_ON_ERROR(status = LWMSG_STATUS_NOT_FOUND);
    }

    for (handle = session->handles; handle; handle = handle->next)
    {
        if (handle->pointer == pointer)
        {
            if (type && strcmp(type, handle->type))
            {
                BAIL_ON_ERROR(status = LWMSG_STATUS_NOT_FOUND);
            }

            if (out_location)
            {
                *out_location = handle->locality;
            }
            if (out_hid)
            {
                *out_hid = handle->hid;
            }
            goto done;
        }
    }

    if (autoreg)
    {
        BAIL_ON_ERROR(status = default_add_handle(
                          session,
                          type,
                          LWMSG_HANDLE_LOCAL,
                          pointer,
                          priv->next_hid++,
                          NULL,
                          &handle));
        *out_location = handle->locality;
        *out_hid = handle->hid;
    }
    else
    {
        BAIL_ON_ERROR(status = LWMSG_STATUS_NOT_FOUND);
    }

done:

    return status;

error:

    goto done;
}

static
LWMsgStatus
default_handle_id_to_pointer(
    LWMsgSessionManager* manager,
    LWMsgSession* session,
    const char* type,
    LWMsgHandleLocation location,
    unsigned long hid,
    LWMsgBool autoreg,
    void** out_ptr
    )
{
    LWMsgStatus status = LWMSG_STATUS_SUCCESS;
    HandleEntry* handle = NULL;

    for (handle = session->handles; handle; handle = handle->next)
    {
        if (handle->hid == hid && handle->locality == location)
        {
            if (type && strcmp(type, handle->type))
            {
                BAIL_ON_ERROR(status = LWMSG_STATUS_NOT_FOUND);
            }

            *out_ptr = handle->pointer;
            goto done;
        }
    }

    if (autoreg)
    {
        BAIL_ON_ERROR(status = default_add_handle(
                          session,
                          type,
                          location,
                          NULL,
                          hid,
                          NULL,
                          &handle));
        *out_ptr = handle->pointer;
    }
    else
    {
        BAIL_ON_ERROR(status = LWMSG_STATUS_NOT_FOUND);
    }

done:

    return status;

error:

    goto done;
}

LWMsgStatus
default_set_session_data (
    LWMsgSessionManager* manager,
    LWMsgSession* session,
    void* data,
    LWMsgSessionDataCleanupFunction cleanup
    )
{
    if (session->cleanup)
    {
        session->cleanup(session->data);
    }

    session->data = data;
    session->cleanup = cleanup;

    return LWMSG_STATUS_SUCCESS;
}

void*
default_get_session_data (
    LWMsgSessionManager* manager,
    LWMsgSession* session
    )
{
    return session->data;
}

static LWMsgSessionManagerClass default_class = 
{
    .private_size = sizeof(DefaultPrivate),
    .construct = default_construct,
    .destruct = default_destruct,
    .enter_session = default_enter_session,
    .leave_session = default_leave_session,
    .register_handle = default_register_handle,
    .unregister_handle = default_unregister_handle,
    .handle_pointer_to_id = default_handle_pointer_to_id,
    .handle_id_to_pointer = default_handle_id_to_pointer,
    .set_session_data = default_set_session_data,
    .get_session_data = default_get_session_data
};
                                         
LWMsgStatus
lwmsg_default_session_manager_new(
    LWMsgSessionManager** out_manager
    )
{
    return lwmsg_session_manager_new(&default_class, out_manager);
}
