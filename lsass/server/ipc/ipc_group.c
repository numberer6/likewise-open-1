/* Editor Settings: expandtabs and use 4 spaces for indentation
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 * -*- mode: c, c-basic-offset: 4 -*- */

/*
 * Copyright Likewise Software    2004-2008
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.  You should have received a copy of the GNU General
 * Public License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * LIKEWISE SOFTWARE MAKES THIS SOFTWARE AVAILABLE UNDER OTHER LICENSING
 * TERMS AS WELL.  IF YOU HAVE ENTERED INTO A SEPARATE LICENSE AGREEMENT
 * WITH LIKEWISE SOFTWARE, THEN YOU MAY ELECT TO USE THE SOFTWARE UNDER THE
 * TERMS OF THAT SOFTWARE LICENSE AGREEMENT INSTEAD OF THE TERMS OF THE GNU
 * GENERAL PUBLIC LICENSE, NOTWITHSTANDING THE ABOVE NOTICE.  IF YOU
 * HAVE QUESTIONS, OR WISH TO REQUEST A COPY OF THE ALTERNATE LICENSING
 * TERMS OFFERED BY LIKEWISE SOFTWARE, PLEASE CONTACT LIKEWISE SOFTWARE AT
 * license@likewisesoftware.com
 */

/*
 * Copyright (C) Likewise Software. All rights reserved.
 *
 * Module Name:
 *
 *        ipc_group.c
 *
 * Abstract:
 *
 *        Likewise Security and Authentication Subsystem (LSASS)
 *
 *        Inter-process communication (Server) API for Groups
 *
 * Authors: Krishna Ganugapati (krishnag@likewisesoftware.com)
 *          Sriram Nambakam (snambakam@likewisesoftware.com)
 */
#include "ipc.h"

static void
LsaSrvCleanupGroupEnumHandle(
    void* pData
    )
{
    LsaSrvEndEnumGroups(
        NULL,
        (HANDLE) pData);
}

LWMsgStatus
LsaSrvIpcAddGroup(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    PLSA_IPC_ERROR pError = NULL;
    // Do not free pGroupInfoList
    PLSA_GROUP_INFO_LIST pGroupInfoList = (PLSA_GROUP_INFO_LIST)pRequest->object;
    PVOID Handle = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    switch (pGroupInfoList->dwGroupInfoLevel)
    {
        case 0:
            dwError = LsaSrvAddGroup(
                            (HANDLE)Handle,
                            0,
                            pGroupInfoList->ppGroupInfoList.ppInfoList0[0]);
            break;
        case 1:
            dwError = LsaSrvAddGroup(
                            (HANDLE)Handle,
                            1,
                            pGroupInfoList->ppGroupInfoList.ppInfoList1[0]);
            break;
        default:
            dwError = LSA_ERROR_INVALID_PARAMETER;
    }

    if (!dwError)
    {
        pResponse->tag = LSA_R_ADD_GROUP_SUCCESS;
        pResponse->object = NULL;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_ADD_GROUP_FAILURE;;
        pResponse->object = pError;
    }

cleanup:
    return MAP_LSA_ERROR_IPC(dwError);

error:
    goto cleanup;
}

LWMsgStatus
LsaSrvIpcFindGroupByName(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    // Do not free pGroupInfo
    PVOID pGroupInfo = NULL;
    PVOID* ppGroupInfoList = NULL;
    PLSA_GROUP_INFO_LIST pResult = NULL;
    PLSA_IPC_FIND_OBJECT_BY_NAME_REQ pReq = pRequest->object;
    PLSA_IPC_ERROR pError = NULL;
    PVOID Handle = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvFindGroupByName(
                       (HANDLE)Handle,
                       pReq->pszName,
                       pReq->FindFlags,
                       pReq->dwInfoLevel,
                       &pGroupInfo);

    if (!dwError)
    {
        dwError = LsaAllocateMemory(sizeof(*pResult),
                                        (PVOID)&pResult);
        BAIL_ON_LSA_ERROR(dwError);

        pResult->dwGroupInfoLevel = pReq->dwInfoLevel;
        pResult->dwNumGroups = 1;
        dwError = LsaAllocateMemory(
                        sizeof(*ppGroupInfoList) * 1,
                        (PVOID*)&ppGroupInfoList);
        BAIL_ON_LSA_ERROR(dwError);

        ppGroupInfoList[0] = pGroupInfo;
        pGroupInfo = NULL;

        switch (pResult->dwGroupInfoLevel)
        {
            case 0:
                pResult->ppGroupInfoList.ppInfoList0 = (PLSA_GROUP_INFO_0*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;
            case 1:
                pResult->ppGroupInfoList.ppInfoList1 = (PLSA_GROUP_INFO_1*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;
            default:
                dwError = LSA_ERROR_INVALID_PARAMETER;
                BAIL_ON_LSA_ERROR(dwError);
        }

        pResponse->tag = LSA_R_GROUP_BY_NAME_SUCCESS;
        pResponse->object = pResult;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_GROUP_BY_NAME_FAILURE;;
        pResponse->object = pError;
    }

cleanup:
    if (pGroupInfo)
    {
        LsaFreeGroupInfo(pReq->dwInfoLevel, pGroupInfo);
    }
    if(ppGroupInfoList)
    {
        LsaFreeGroupInfoList(pReq->dwInfoLevel, ppGroupInfoList, 1);
    }

    return MAP_LSA_ERROR_IPC(dwError);

error:
    if(pResult)
    {
        LsaFreeIpcGroupInfoList(pResult);
    }

    goto cleanup;
}

LWMsgStatus
LsaSrvIpcFindGroupById(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    PVOID pGroupInfo = NULL;
    PVOID* ppGroupInfoList = NULL;
    PLSA_GROUP_INFO_LIST pResult = NULL;
    PLSA_IPC_FIND_OBJECT_BY_ID_REQ pReq = pRequest->object;
    PLSA_IPC_ERROR pError = NULL;
    PVOID Handle = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvFindGroupById(
                       (HANDLE)Handle,
                       pReq->id,
                       pReq->FindFlags,
                       pReq->dwInfoLevel,
                       &pGroupInfo);

    if (!dwError)
    {
        dwError = LsaAllocateMemory(sizeof(*pResult),
                                    (PVOID)&pResult);
        BAIL_ON_LSA_ERROR(dwError);

        pResult->dwGroupInfoLevel = pReq->dwInfoLevel;
        pResult->dwNumGroups = 1;
        dwError = LsaAllocateMemory(
                        sizeof(*ppGroupInfoList) * 1,
                        (PVOID*)&ppGroupInfoList);
        BAIL_ON_LSA_ERROR(dwError);

        ppGroupInfoList[0] = pGroupInfo;
        pGroupInfo = NULL;

        switch (pResult->dwGroupInfoLevel)
        {
            case 0:
                pResult->ppGroupInfoList.ppInfoList0 = (PLSA_GROUP_INFO_0*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;
            case 1:
                pResult->ppGroupInfoList.ppInfoList1 = (PLSA_GROUP_INFO_1*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;
            default:
                dwError = LSA_ERROR_INVALID_PARAMETER;
                BAIL_ON_LSA_ERROR(dwError);
        }

        pResponse->tag = LSA_R_GROUP_BY_ID_SUCCESS;
        pResponse->object = pResult;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_GROUP_BY_ID_FAILURE;;
        pResponse->object = pError;
    }

cleanup:
    if (pGroupInfo)
    {
        LsaFreeGroupInfo(pReq->dwInfoLevel, pGroupInfo);
    }
    if(ppGroupInfoList)
    {
        LsaFreeGroupInfoList(pReq->dwInfoLevel, ppGroupInfoList, 1);
    }

    return MAP_LSA_ERROR_IPC(dwError);

error:
    if(pResult)
    {
        LsaFreeIpcGroupInfoList(pResult);
    }

    goto cleanup;
}

LWMsgStatus
LsaSrvIpcGetGroupsForUser(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    PVOID* ppGroupInfoList = NULL;
    DWORD dwNumGroupsFound = 0;
    PLSA_GROUP_INFO_LIST pResult = NULL;
    PLSA_IPC_FIND_OBJECT_BY_ID_REQ pReq = pRequest->object;
    PLSA_IPC_ERROR pError = NULL;
    PVOID Handle = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvGetGroupsForUser(
                       (HANDLE)Handle,
                       pReq->id,
                       pReq->FindFlags,
                       pReq->dwInfoLevel,
                       &dwNumGroupsFound,
                       &ppGroupInfoList);

    if (!dwError)
    {
        dwError = LsaAllocateMemory(sizeof(*pResult),
                                        (PVOID)&pResult);
        BAIL_ON_LSA_ERROR(dwError);

        pResult->dwGroupInfoLevel = pReq->dwInfoLevel;
        pResult->dwNumGroups = dwNumGroupsFound;

        switch (pResult->dwGroupInfoLevel)
        {
            case 0:
                pResult->ppGroupInfoList.ppInfoList0 = (PLSA_GROUP_INFO_0*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;
            case 1:
                pResult->ppGroupInfoList.ppInfoList1 = (PLSA_GROUP_INFO_1*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;
            default:
                dwError = LSA_ERROR_INVALID_PARAMETER;
                BAIL_ON_LSA_ERROR(dwError);
        }

        pResponse->tag = LSA_R_GROUPS_FOR_USER_SUCCESS;
        pResponse->object = pResult;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_GROUPS_FOR_USER_FAILURE;
        pResponse->object = pError;
    }

cleanup:
    if(ppGroupInfoList)
    {
        LsaFreeGroupInfoList(pReq->dwInfoLevel, ppGroupInfoList, dwNumGroupsFound);
    }

    return MAP_LSA_ERROR_IPC(dwError);

error:
    if(pResult)
    {
        LsaFreeIpcGroupInfoList(pResult);
    }

    goto cleanup;
}

LWMsgStatus
LsaSrvIpcBeginEnumGroups(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    PLSA_IPC_BEGIN_ENUM_GROUPS_REQ pReq = pRequest->object;
    PLSA_IPC_ERROR pError = NULL;
    PVOID Handle = NULL;
    PVOID hResume = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvBeginEnumGroups(
                        (HANDLE)Handle,
                        pReq->dwInfoLevel,
                        pReq->dwNumMaxRecords,
                        pReq->bCheckGroupMembersOnline,
                        pReq->FindFlags,
                        &hResume);

    if (!dwError)
    {
        dwError = MAP_LWMSG_ERROR(lwmsg_assoc_register_handle(
                                      assoc,
                                      "EnumGroups",
                                      hResume,
                                      LsaSrvCleanupGroupEnumHandle));
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_BEGIN_ENUM_GROUPS_SUCCESS;
        pResponse->object = hResume;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_BEGIN_ENUM_GROUPS_FAILURE;
        pResponse->object = pError;
    }

cleanup:

    return MAP_LSA_ERROR_IPC(dwError);

error:

    if(hResume)
    {
        LsaSrvCleanupGroupEnumHandle(hResume);
    }

    goto cleanup;
}

LWMsgStatus
LsaSrvIpcEnumGroups(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    PVOID* ppGroupInfoList = NULL;
    DWORD  dwGroupInfoLevel = 0;
    DWORD  dwNumGroupsFound = 0;
    PLSA_GROUP_INFO_LIST pResult = NULL;
    PLSA_IPC_ERROR pError = NULL;
    PVOID Handle = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvEnumGroups(
        Handle,
        (HANDLE) pRequest->object,
        &dwGroupInfoLevel,
        &ppGroupInfoList,
        &dwNumGroupsFound);

    if (!dwError)
    {
        dwError = LsaAllocateMemory(sizeof(*pResult),
                                    (PVOID)&pResult);
        BAIL_ON_LSA_ERROR(dwError);

        pResult->dwGroupInfoLevel = dwGroupInfoLevel;
        pResult->dwNumGroups = dwNumGroupsFound;
        switch (pResult->dwGroupInfoLevel)
        {
            case 0:
                pResult->ppGroupInfoList.ppInfoList0 = (PLSA_GROUP_INFO_0*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;

            case 1:
                pResult->ppGroupInfoList.ppInfoList1 = (PLSA_GROUP_INFO_1*)ppGroupInfoList;
                ppGroupInfoList = NULL;
                break;

            default:
                dwError = LSA_ERROR_INVALID_PARAMETER;
                BAIL_ON_LSA_ERROR(dwError);
        }

        pResponse->tag = LSA_R_ENUM_GROUPS_SUCCESS;
        pResponse->object = pResult;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_ENUM_GROUPS_FAILURE;;
        pResponse->object = pError;
    }

cleanup:
    if(ppGroupInfoList)
    {
        LsaFreeGroupInfoList(dwGroupInfoLevel, ppGroupInfoList, dwNumGroupsFound);
    }

    return MAP_LSA_ERROR_IPC(dwError);

error:
    if(pResult)
    {
        LsaFreeIpcGroupInfoList(pResult);
    }

    goto cleanup;
}

LWMsgStatus
LsaSrvIpcEndEnumGroups(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    PLSA_IPC_ERROR pError = NULL;
    PVOID Handle = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvEndEnumGroups(
        Handle,
        (HANDLE) pRequest->object);

    if (!dwError)
    {
        dwError = MAP_LWMSG_ERROR(lwmsg_assoc_unregister_handle(assoc, pRequest->object, LWMSG_FALSE));
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_END_ENUM_GROUPS_SUCCESS;
        pResponse->object = NULL;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_END_ENUM_GROUPS_FAILURE;
        pResponse->object = pError;
    }

cleanup:
    return MAP_LSA_ERROR_IPC(dwError);

error:
    goto cleanup;
}

LWMsgStatus
LsaSrvIpcDeleteGroup(
    LWMsgAssoc* assoc,
    const LWMsgMessage* pRequest,
    LWMsgMessage* pResponse,
    void* data
    )
{
    DWORD dwError = 0;
    PLSA_IPC_ERROR pError = NULL;
    PVOID Handle = NULL;

    dwError = MAP_LWMSG_ERROR(lwmsg_assoc_get_session_data(assoc, (PVOID*) (PVOID) &Handle));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvDeleteGroup(
                        (HANDLE)Handle,
                        *((PDWORD)pRequest->object));

    if (!dwError)
    {
        pResponse->tag = LSA_R_DELETE_GROUP_SUCCESS;
        pResponse->object = NULL;
    }
    else
    {
        dwError = LsaSrvIpcCreateError(dwError, NULL, &pError);
        BAIL_ON_LSA_ERROR(dwError);

        pResponse->tag = LSA_R_DELETE_GROUP_FAILURE;
        pResponse->object = pError;
    }

cleanup:
    return MAP_LSA_ERROR_IPC(dwError);

error:
    goto cleanup;
}
