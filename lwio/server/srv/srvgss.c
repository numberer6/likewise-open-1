#include "includes.h"
#include "srvgss_p.h"

NTSTATUS
SrvGssAcquireContext(
    HANDLE  hGssOrig,
    PHANDLE phGssNew
    )
{
    NTSTATUS ntStatus = 0;
    PSRV_KRB5_CONTEXT pContext = (PSRV_KRB5_CONTEXT)hGssOrig;

    if (!pContext)
    {
        ntStatus = SrvGssNewContext(&pContext);
        BAIL_ON_NT_STATUS(ntStatus);
    }
    else
    {
        InterlockedIncrement(&pContext->refcount);
    }

    *phGssNew = (HANDLE)pContext;

cleanup:

    return ntStatus;

error:

    *phGssNew = (HANDLE)NULL;

    goto cleanup;
}

BOOLEAN
SrvGssNegotiateIsComplete(
    HANDLE hGss,
    HANDLE hGssNegotiate
    )
{
    PSRV_GSS_NEGOTIATE_CONTEXT pGssNegotiate = (PSRV_GSS_NEGOTIATE_CONTEXT)hGssNegotiate;
    return pGssNegotiate->state == SRV_GSS_CONTEXT_STATE_COMPLETE;
}

NTSTATUS
SrvGssGetSessionKey(
    HANDLE hGss,
    HANDLE hGssNegotiate,
    PBYTE* ppSessionKey,
    PULONG pulSessionKeyLength
    )
{
    NTSTATUS ntStatus = 0;
    PSRV_GSS_NEGOTIATE_CONTEXT pGssNegotiate = (PSRV_GSS_NEGOTIATE_CONTEXT)hGssNegotiate;
    ULONG ulMinorStatus = 0;
    gss_name_t initiatorName = {0};
    gss_name_t acceptorName = {0};
    gss_buffer_desc sessionKey = GSS_C_EMPTY_BUFFER;
    PBYTE pSessionKey = NULL;

    if (!SrvGssNegotiateIsComplete(hGss, hGssNegotiate))
    {
        ntStatus = STATUS_DATA_ERROR;
        BAIL_ON_NT_STATUS(ntStatus);
    }

    ntStatus = gss_inquire_context2(
                    &ulMinorStatus,
                    *pGssNegotiate->pGssContext,
                    &initiatorName,
                    &acceptorName,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    &sessionKey);

    srv_display_status("gss_inquire_context2", ntStatus, ulMinorStatus);
    BAIL_ON_SEC_ERROR(ntStatus);

    assert(sessionKey.length > 0);

    ntStatus = SMBAllocateMemory(
                    sessionKey.length * sizeof(BYTE),
                    (PVOID*)&pSessionKey);
    BAIL_ON_NT_STATUS(ntStatus);

    memcpy(pSessionKey, sessionKey.value, sessionKey.length);

    *ppSessionKey = pSessionKey;
    *pulSessionKeyLength = sessionKey.length;

cleanup:

    gss_release_name(&ulMinorStatus, &initiatorName);
    gss_release_name(&ulMinorStatus, &acceptorName);
    gss_release_buffer(&ulMinorStatus, &sessionKey);

    return ntStatus;

sec_error:

    ntStatus = SMB_ERROR_GSS;

error:

    *ppSessionKey = NULL;
    *pulSessionKeyLength = 0;

    SMB_SAFE_FREE_MEMORY(pSessionKey);

    goto cleanup;
}

NTSTATUS
SrvGssBeginNegotiate(
    HANDLE  hGss,
    PHANDLE phGssResume
    )
{
    NTSTATUS ntStatus = 0;
    PSRV_GSS_NEGOTIATE_CONTEXT pGssNegotiate = NULL;

    ntStatus = SMBAllocateMemory(
                    sizeof(SRV_GSS_NEGOTIATE_CONTEXT),
                    (PVOID*)&pGssNegotiate);
    BAIL_ON_NT_STATUS(ntStatus);

    pGssNegotiate->state = SRV_GSS_CONTEXT_STATE_INITIAL;

    ntStatus = SMBAllocateMemory(
                        sizeof(gss_ctx_id_t),
                        (PVOID*)&pGssNegotiate->pGssContext);
    BAIL_ON_NT_STATUS(ntStatus);

    *pGssNegotiate->pGssContext = GSS_C_NO_CONTEXT;

    *phGssResume = (HANDLE)pGssNegotiate;

cleanup:

    return ntStatus;

error:

    *phGssResume = NULL;

    if (pGssNegotiate)
    {
        SrvGssEndNegotiate(hGss, (HANDLE)pGssNegotiate);
    }

    goto cleanup;
}

NTSTATUS
SrvGssNegotiate(
    HANDLE  hGss,
    HANDLE  hGssResume,
    PBYTE   pSecurityInputBlob,
    ULONG   ulSecurityInputBlobLen,
    PBYTE*  ppSecurityOutputBlob,
    ULONG*  pulSecurityOutputBloblen
    )
{
    NTSTATUS ntStatus = 0;
    PSRV_KRB5_CONTEXT pGssContext = (PSRV_KRB5_CONTEXT)hGss;
    PSRV_GSS_NEGOTIATE_CONTEXT pGssNegotiate = (PSRV_GSS_NEGOTIATE_CONTEXT)hGssResume;
    PBYTE pSecurityBlob = NULL;
    ULONG ulSecurityBlobLen = 0;

    switch(pGssNegotiate->state)
    {
        case SRV_GSS_CONTEXT_STATE_INITIAL:

            if (pSecurityInputBlob)
            {
                ntStatus = STATUS_INVALID_PARAMETER_3;
                BAIL_ON_NT_STATUS(ntStatus);
            }

            ntStatus = SrvGssInitNegotiate(
                            pGssContext,
                            pGssNegotiate,
                            pSecurityInputBlob,
                            ulSecurityInputBlobLen,
                            &pSecurityBlob,
                            &ulSecurityBlobLen);

            break;

        case SRV_GSS_CONTEXT_STATE_NEGOTIATE:

            if (!pSecurityInputBlob)
            {
                ntStatus = STATUS_INVALID_PARAMETER_3;
                BAIL_ON_NT_STATUS(ntStatus);
            }

            ntStatus = SrvGssContinueNegotiate(
                            pGssContext,
                            pGssNegotiate,
                            pSecurityInputBlob,
                            ulSecurityInputBlobLen,
                            &pSecurityBlob,
                            &ulSecurityBlobLen);

            break;

        case SRV_GSS_CONTEXT_STATE_COMPLETE:

            break;

    }

    BAIL_ON_NT_STATUS(ntStatus);

error:

    *ppSecurityOutputBlob = pSecurityBlob;
    *pulSecurityOutputBloblen = ulSecurityBlobLen;

    return ntStatus;
}

VOID
SrvGssEndNegotiate(
    HANDLE hGss,
    HANDLE hGssResume
    )
{
    ULONG ulMinorStatus = 0;
    PSRV_GSS_NEGOTIATE_CONTEXT pGssNegotiateContext = NULL;

    pGssNegotiateContext = (PSRV_GSS_NEGOTIATE_CONTEXT)hGssResume;

    if (pGssNegotiateContext->pGssContext &&
        (*pGssNegotiateContext->pGssContext != GSS_C_NO_CONTEXT))
    {
        gss_delete_sec_context(
                        &ulMinorStatus,
                        pGssNegotiateContext->pGssContext,
                        GSS_C_NO_BUFFER);

        SMBFreeMemory(pGssNegotiateContext->pGssContext);
    }

    SMBFreeMemory(pGssNegotiateContext);
}

VOID
SrvGssReleaseContext(
    HANDLE hGss
    )
{
    PSRV_KRB5_CONTEXT pContext = (PSRV_KRB5_CONTEXT)hGss;

    if (InterlockedDecrement(&pContext->refcount) == 0)
    {
        SrvGssFreeContext(pContext);
    }
}

static
NTSTATUS
SrvGssNewContext(
    PSRV_KRB5_CONTEXT* ppContext
    )
{
    NTSTATUS ntStatus = 0;
    CHAR     szHostname[256];
    PSTR     pszDomain = NULL;
    PSTR     pszCachePath = NULL;
    PSRV_KRB5_CONTEXT pContext = NULL;

    ntStatus = LWNetGetCurrentDomain(&pszDomain);
    BAIL_ON_NT_STATUS(ntStatus);

    if (gethostname(szHostname, sizeof(szHostname)) != 0)
    {
        ntStatus = LwUnixErrnoToNtStatus(errno);
        BAIL_ON_NT_STATUS(ntStatus);
    }

    ntStatus = SMBAllocateMemory(
                    sizeof(SRV_KRB5_CONTEXT),
                    (PVOID*)&pContext);
    BAIL_ON_NT_STATUS(ntStatus);

    pContext->refcount = 1;

    pthread_mutex_init(&pContext->mutex, NULL);
    pContext->pMutex = &pContext->mutex;

    ntStatus = SMBAllocateStringPrintf(
                    &pContext->pszMachinePrincipal,
                    "%s$@%s",
                    szHostname,
                    pszDomain);
    BAIL_ON_NT_STATUS(ntStatus);

    SMBStrToUpper(pContext->pszMachinePrincipal);

    ntStatus = SMBAllocateString(
                    SRV_KRB5_CACHE_PATH,
                    &pszCachePath);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = SrvGetTGTFromKeytab(
                    pContext->pszMachinePrincipal,
                    NULL,
                    pszCachePath,
                    &pContext->ticketExpiryTime);
    BAIL_ON_NT_STATUS(ntStatus);

    pContext->pszCachePath = pszCachePath;

    *ppContext = pContext;

cleanup:

    if (pszDomain)
    {
        LWNetFreeString(pszDomain);
    }

    return ntStatus;

error:

    *ppContext = NULL;

    if (pContext)
    {
        SrvGssFreeContext(pContext);
    }

    SMB_SAFE_FREE_STRING(pszCachePath);

    goto cleanup;
}

static
NTSTATUS
SrvGssInitNegotiate(
    PSRV_KRB5_CONTEXT          pGssContext,
    PSRV_GSS_NEGOTIATE_CONTEXT pGssNegotiate,
    PBYTE                      pSecurityInputBlob,
    ULONG                      ulSecurityInputBlobLen,
    PBYTE*                     ppSecurityOutputBlob,
    ULONG*                     pulSecurityOutputBloblen
    )
{
    NTSTATUS ntStatus = 0;
    ULONG ulMajorStatus = 0;
    ULONG ulMinorStatus = 0;
    gss_buffer_desc input_name = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc input_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc output_desc = GSS_C_EMPTY_BUFFER;
    gss_name_t      target_name = GSS_C_NO_NAME;
    PBYTE pSessionKey = NULL;
    ULONG ulSessionKeyLength = 0;
    ULONG ret_flags = 0;
    PSTR  pszCurrentCachePath = NULL;
    BOOLEAN bInLock = FALSE;

    static gss_OID_desc gss_spnego_mech_oid_desc =
      {6, (void *)"\x2b\x06\x01\x05\x05\x02"};

    SMB_LOCK_MUTEX(bInLock, pGssContext->pMutex);

    ntStatus = SrvGssRenew(pGssContext);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = SrvSetDefaultKrb5CachePath(
                    pGssContext->pszCachePath,
                    &pszCurrentCachePath);
    BAIL_ON_NT_STATUS(ntStatus);

    input_name.value = pGssContext->pszMachinePrincipal;
    input_name.length = strlen(pGssContext->pszMachinePrincipal);

    ulMajorStatus = gss_import_name(
                        (OM_uint32 *)&ulMinorStatus,
                        &input_name,
                        (gss_OID) gss_nt_krb5_name,
                        &target_name);

    srv_display_status("gss_import_name", ulMajorStatus, ulMinorStatus);

    BAIL_ON_SEC_ERROR(ulMajorStatus);

    ulMajorStatus = gss_init_sec_context(
                            (OM_uint32 *)&ulMinorStatus,
                            GSS_C_NO_CREDENTIAL,
                            pGssNegotiate->pGssContext,
                            target_name,
                            &gss_spnego_mech_oid_desc,
                            0,
                            0,
                            GSS_C_NO_CHANNEL_BINDINGS,
                            &input_desc,
                            NULL,
                            &output_desc,
                            &ret_flags,
                            NULL);

    srv_display_status("gss_init_sec_context", ulMajorStatus, ulMinorStatus);

    BAIL_ON_SEC_ERROR(ulMajorStatus);

    switch (ulMajorStatus)
    {
        case GSS_S_CONTINUE_NEEDED:

            pGssNegotiate->state = SRV_GSS_CONTEXT_STATE_NEGOTIATE;

            break;

        case GSS_S_COMPLETE:

            pGssNegotiate->state = SRV_GSS_CONTEXT_STATE_COMPLETE;

            break;

        default:

            ntStatus = SMB_ERROR_GSS;
            BAIL_ON_NT_STATUS(ntStatus);

            break;
    }

    if (output_desc.length)
    {
        ntStatus = SMBAllocateMemory(
                        output_desc.length,
                        (PVOID*)&pSessionKey);
        BAIL_ON_NT_STATUS(ntStatus);

        memcpy(pSessionKey, output_desc.value, output_desc.length);

        ulSessionKeyLength = output_desc.length;
    }

    *ppSecurityOutputBlob = pSessionKey;
    *pulSecurityOutputBloblen = ulSessionKeyLength;

cleanup:

    SMB_UNLOCK_MUTEX(bInLock, pGssContext->pMutex);

    gss_release_buffer(&ulMinorStatus, &output_desc);

    gss_release_name(&ulMinorStatus, &target_name);

    if (pGssNegotiate->pGssContext &&
        (*pGssNegotiate->pGssContext != GSS_C_NO_CONTEXT))
    {
        gss_delete_sec_context(
                        &ulMinorStatus,
                        pGssNegotiate->pGssContext,
                        GSS_C_NO_BUFFER);

        *pGssNegotiate->pGssContext = GSS_C_NO_CONTEXT;
    }

    if (pszCurrentCachePath)
    {
        SrvSetDefaultKrb5CachePath(
            pszCurrentCachePath,
            NULL);

        SMBFreeMemory(pszCurrentCachePath);
    }

    return ntStatus;

sec_error:

    ntStatus = SMB_ERROR_GSS;

error:

    *ppSecurityOutputBlob = NULL;
    *pulSecurityOutputBloblen = 0;

    SMB_SAFE_FREE_MEMORY(pSessionKey);

    goto cleanup;
}

static
NTSTATUS
SrvGssContinueNegotiate(
    PSRV_KRB5_CONTEXT          pGssContext,
    PSRV_GSS_NEGOTIATE_CONTEXT pGssNegotiate,
    PBYTE                      pSecurityInputBlob,
    ULONG                      ulSecurityInputBlobLen,
    PBYTE*                     ppSecurityOutputBlob,
    ULONG*                     pulSecurityOutputBloblen
    )
{
    NTSTATUS ntStatus = 0;
    ULONG ulMajorStatus = 0;
    ULONG ulMinorStatus = 0;
    gss_buffer_desc input_desc = {0};
    gss_buffer_desc output_desc = {0};
    ULONG ret_flags = 0;
    PBYTE pSecurityBlob = NULL;
    ULONG ulSecurityBlobLength = 0;
    BOOLEAN bInLock = FALSE;

    static gss_OID_desc gss_spnego_mech_oid_desc =
                              {6, (void *)"\x2b\x06\x01\x05\x05\x02"};
    static gss_OID gss_spnego_mech_oid = &gss_spnego_mech_oid_desc;

    SMB_LOCK_MUTEX(bInLock, pGssContext->pMutex);

    input_desc.length = ulSecurityInputBlobLen;
    input_desc.value = pSecurityInputBlob;

    ulMajorStatus = gss_accept_sec_context(
                        (OM_uint32 *)&ulMinorStatus,
                        pGssNegotiate->pGssContext,
                        NULL,
                        &input_desc,
                        NULL,
                        NULL,
                        &gss_spnego_mech_oid,
                        &output_desc,
                        &ret_flags,
                        NULL,
                        NULL);

    srv_display_status("gss_accept_sec_context", ulMajorStatus, ulMinorStatus);

    BAIL_ON_SEC_ERROR(ulMajorStatus);

    switch (ulMajorStatus)
    {
        case GSS_S_CONTINUE_NEEDED:

            pGssNegotiate->state = SRV_GSS_CONTEXT_STATE_NEGOTIATE;

            break;

        case GSS_S_COMPLETE:

            pGssNegotiate->state = SRV_GSS_CONTEXT_STATE_COMPLETE;

            break;

        default:

            ntStatus = SMB_ERROR_GSS;
            BAIL_ON_NT_STATUS(ntStatus);

            break;
    }

    if (output_desc.length)
    {
        ntStatus = SMBAllocateMemory(
                        output_desc.length,
                        (PVOID*)&pSecurityBlob);
        BAIL_ON_NT_STATUS(ntStatus);

        memcpy(pSecurityBlob, output_desc.value, output_desc.length);

        ulSecurityBlobLength = output_desc.length;
    }

    *ppSecurityOutputBlob = pSecurityBlob;
    *pulSecurityOutputBloblen = ulSecurityBlobLength;

cleanup:

    SMB_UNLOCK_MUTEX(bInLock, pGssContext->pMutex);

    gss_release_buffer(&ulMinorStatus, &output_desc);

    return ntStatus;

sec_error:

    ntStatus = SMB_ERROR_GSS;

error:

    *ppSecurityOutputBlob = NULL;
    *pulSecurityOutputBloblen = 0;

    SMB_SAFE_FREE_MEMORY(pSecurityBlob);

    goto cleanup;
}

static
VOID
SrvGssFreeContext(
    PSRV_KRB5_CONTEXT pContext
    )
{
    if (!IsNullOrEmptyString(pContext->pszCachePath))
    {
        NTSTATUS ntStatus = 0;

        ntStatus = SrvDestroyKrb5Cache(pContext->pszCachePath);
        if (ntStatus)
        {
            SMB_LOG_ERROR("Failed to destroy kerberos cache path [%s][code:%d]",
                          pContext->pszCachePath,
                          ntStatus);
        }
    }

    SMB_SAFE_FREE_STRING(pContext->pszCachePath);
    SMB_SAFE_FREE_STRING(pContext->pszMachinePrincipal);

    if (pContext->pMutex)
    {
        pthread_mutex_destroy(pContext->pMutex);
    }
}

static
void
srv_display_status(
    PCSTR     pszId,
    OM_uint32 maj_stat,
    OM_uint32 min_stat
    )
{
     srv_display_status_1(pszId, maj_stat, GSS_C_GSS_CODE);
     srv_display_status_1(pszId, min_stat, GSS_C_MECH_CODE);
}

static
void
srv_display_status_1(
    PCSTR     pszId,
    OM_uint32 code,
    int       type
    )
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc msg;
    OM_uint32 msg_ctx;

    if ( code == 0 )
    {
        return;
    }

    msg_ctx = 0;
    while (1)
    {
        maj_stat = gss_display_status(&min_stat, code,
                                      type, GSS_C_NULL_OID,
                                      &msg_ctx, &msg);

        switch(code)
        {
#ifdef WIN32
            case SEC_E_OK:
            case SEC_I_CONTINUE_NEEDED:
#else
            case GSS_S_COMPLETE:
            case GSS_S_CONTINUE_NEEDED:
#endif
                SMB_LOG_VERBOSE("GSS-API error calling %s: %d (%s)\n",
                        pszId, code,
                        (char *)msg.value);
                break;

            default:

                SMB_LOG_ERROR("GSS-API error calling %s: %d (%s)\n",
                        pszId, code,
                        (char *)msg.value);
        }

        (void) gss_release_buffer(&min_stat, &msg);

        if (!msg_ctx)
            break;
    }
}

static
NTSTATUS
SrvGssRenew(
   PSRV_KRB5_CONTEXT pContext
   )
{
    NTSTATUS ntStatus = 0;

    if (!pContext->ticketExpiryTime ||
        difftime(time(NULL), pContext->ticketExpiryTime) >  60 * 60)
    {
        ntStatus = SrvGetTGTFromKeytab(
                        pContext->pszMachinePrincipal,
                        NULL,
                        pContext->pszCachePath,
                        &pContext->ticketExpiryTime);
    }

    return ntStatus;
}

static
NTSTATUS
SrvGetTGTFromKeytab(
    PCSTR   pszUserName,
    PCSTR   pszPassword,
    PCSTR   pszCachePath,
    time_t* pGoodUntilTime
    )
{
    NTSTATUS ntStatus = 0;
    krb5_error_code ret = 0;
    krb5_context ctx = NULL;
    krb5_creds creds = { 0 };
    krb5_ccache cc = NULL;
    krb5_keytab keytab = 0;
    krb5_principal client_principal = NULL;
    krb5_get_init_creds_opt opts;

    ret = krb5_init_context(&ctx);
    BAIL_ON_KRB_ERROR(ctx, ret);

    ret = krb5_parse_name(ctx, pszUserName, &client_principal);
    BAIL_ON_KRB_ERROR(ctx, ret);

    /* use krb5_cc_resolve to get an alternate cache */
    ret = krb5_cc_resolve(ctx, pszCachePath, &cc);
    BAIL_ON_KRB_ERROR(ctx, ret);

    ret = krb5_kt_default(ctx, &keytab);
    BAIL_ON_KRB_ERROR(ctx, ret);

    krb5_get_init_creds_opt_init(&opts);
    krb5_get_init_creds_opt_set_forwardable(&opts, TRUE);

    ret = krb5_get_init_creds_keytab(
                    ctx,
                    &creds,
                    client_principal,
                    keytab,
                    0,    /* start time     */
                    NULL, /* in_tkt_service */
                    &opts  /* options        */
                    );
    BAIL_ON_KRB_ERROR(ctx, ret);

    ret = krb5_cc_initialize(ctx, cc, client_principal);
    BAIL_ON_KRB_ERROR(ctx, ret);

    ret = krb5_cc_store_cred(ctx, cc, &creds);
    BAIL_ON_KRB_ERROR(ctx, ret);

    if (pGoodUntilTime)
    {
        *pGoodUntilTime = creds.times.endtime;
    }

error:

    if (creds.client == client_principal)
    {
        creds.client = NULL;
    }

    if (ctx)
    {
        if (client_principal)
        {
            krb5_free_principal(ctx, client_principal);
        }

        if (keytab) {
            krb5_kt_close(ctx, keytab);
        }

        if (cc) {
            krb5_cc_close(ctx, cc);
        }

        krb5_free_cred_contents(ctx, &creds);

        krb5_free_context(ctx);
    }

    return ntStatus;
}

static
NTSTATUS
SrvDestroyKrb5Cache(
    PCSTR pszCachePath
    )
{
    NTSTATUS ntStatus = 0;
    krb5_error_code ret = 0;
    krb5_context ctx = NULL;
    krb5_ccache cc = NULL;

    ret = krb5_init_context(&ctx);
    BAIL_ON_KRB_ERROR(ctx, ret);

    /* use krb5_cc_resolve to get an alternate cache */
    ret = krb5_cc_resolve(ctx, pszCachePath, &cc);
    BAIL_ON_KRB_ERROR(ctx, ret);

    ret = krb5_cc_destroy(ctx, cc);
    if (ret != 0) {
        if (ret != KRB5_FCC_NOFILE) {
            BAIL_ON_KRB_ERROR(ctx, ret);
        } else {
            ret = 0;
        }
    }

error:

    if (ctx)
    {
       krb5_free_context(ctx);
    }

    return ntStatus;
}

static
NTSTATUS
SrvSetDefaultKrb5CachePath(
    PCSTR pszCachePath,
    PSTR* ppszOrigCachePath
    )
{
    NTSTATUS ntStatus = 0;
    ULONG ulMajorStatus = 0;
    ULONG ulMinorStatus = 0;
    PSTR  pszOrigCachePath = NULL;

    // Set the default for gss
    ulMajorStatus = gss_krb5_ccache_name(
                            (OM_uint32 *)&ulMinorStatus,
                            pszCachePath,
                            (ppszOrigCachePath) ? (const char**)&pszOrigCachePath : NULL);
    BAIL_ON_SEC_ERROR(ulMajorStatus);

    if (ppszOrigCachePath)
    {
        if (!IsNullOrEmptyString(pszOrigCachePath))
        {
            ntStatus = SMBAllocateString(
                            pszOrigCachePath,
                            ppszOrigCachePath);
            BAIL_ON_NT_STATUS(ntStatus);
        }
        else
        {
            *ppszOrigCachePath = NULL;
        }
    }

    SMB_LOG_DEBUG("Cache path set to [%s]", SMB_SAFE_LOG_STRING(pszCachePath));

cleanup:

    return ntStatus;

sec_error:
error:

    if (ppszOrigCachePath)
    {
        *ppszOrigCachePath = NULL;
    }

    goto cleanup;
}
