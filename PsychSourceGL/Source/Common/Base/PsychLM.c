/*
 * Psychtoolbox-3/PsychSourceGL/Source/Common/Base/PsychLM.c
 *
 * AUTHORS:
 *
 * kleiner@mi-incubator.com mk on behalf of "Medical Innovations Incubator GmbH, Tübingen, Germany"
 *
 * DESCRIPTION:
 *
 * Implementation of license checking, license validation, and license management
 * on Psychtoolbox target platforms for which software licenses are required for use
 * of the prebuilt and tested mex files bundled with the Psychtoolbox distribution.
 *
 * This is utilizing the LexActivator client libraries and api from our licensing
 * solutions provider Cryptlex: https://cryptlex.com
 *
 * Copyright (c) 2024 Medical Innovations Incubator GmbH, Tübingen, Germany
 *
 * Licensed under the MIT license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

// Need Unicode compilation on MS-Windows, standard char on non-Windows:
// Following UNICODE define must be included before Psych.h!
#ifdef _WIN32
#define UNICODE
#endif

// All the defines and includes we need from Psychtoolbox:
#include "Psych.h"

#ifndef PSYCH_NOLM

// Need Unicode compilation on MS-Windows, standard char on non-Windows,
// and for that some more conditional defines:
#if PSYCH_SYSTEM == PSYCH_WINDOWS
#    define _UNICODE
#    include <tchar.h>
#else
#    define _T(x) x
typedef char TCHAR;
#include <dlfcn.h>
#endif

// Include the correct link-library on Windows during MSVC build for non-Octave.
// Path is relative to the Psychtoolbox-3/PsychSourceGL/Source/ folder:
#pragma comment(lib, "../../../LexActivator.lib")

// Include file with LexActivator client api definitions:
// Assume it is located outside the Psychtoolbox-3 main directory,
// stored in the same directory in which the Psychtoolbox-3 directory,
// ie. alongside of it:
#include "../../../../../LexActivator.h"

// For time() function et al.:
#include <time.h>

// LexActivator initialized?
static psych_bool lminitialized = FALSE;

// Debug output flag:
static unsigned int lmdebug = 0;

// Current licensing status, start with -1 = "unknown":
static int licenseStatus = -1;

static char activationMetaDataString[255] = { 0 };

static const char* LMErrorString(int hr)
{
    static char errorCodeString[32];

    switch(hr) {
        case LA_OK: return("Success.");
        case LA_FAIL: return("General failure.");
        case LA_E_LICENSE_KEY: return("Invalid license key, or no license key enrolled.");
        case LA_E_LICENSE_TYPE: return("Invalid license type. Don't use a floating license.");
        case LA_E_INET: return("Network connection to the server failed. Check your internet connection and possibly proxy settings.");
        case LA_E_NET_PROXY: return("Invalid network proxy URL specified.");
        case LA_E_RATE_LIMIT: return("Rate limit for server reached. Please try again later.");
        case LA_E_SERVER: return("License server error.");
        case LA_E_CLIENT: return("License client error.");
        case LA_E_HOST_URL: return("Invalid Cryptlex host URL specified.");
        case LA_E_ACTIVATION_LIMIT: return("The license has already been activated with the maximum number of computers.");
        case LA_EXPIRED: return("The license has expired or the system time has been tampered with. Ensure your time, timezone, and date settings are correct.");
        case LA_E_REVOKED: return("The license key has been revoked.");
        case LA_SUSPENDED: return("The license has been suspended.");
        case LA_E_ACTIVATION_NOT_FOUND: return("The license activation was deleted on the server.");
        case LA_E_PRODUCT_VERSION_NOT_LINKED: return("No product version is linked with the license.");
        case LA_E_FILE_PATH: return("Invalid path to product details file.");
        case LA_E_PRODUCT_FILE: return("The product details file is invalid or corrupt.");
        case LA_E_PRODUCT_DATA: return("The product data is invalid.");
        case LA_E_PRODUCT_ID: return("The product id is incorrect.");
        case LA_E_TRIAL_NOT_ALLOWED: return("Free trials are not supported.");
        case LA_E_TIME: return("Computer time disagrees with current network time. Please readjust your clock.");
        case LA_E_TIME_MODIFIED: return("Computer clock has been tampered with, ie. backdated. Naughty! Fix this!");
        case LA_E_BUFFER_SIZE: return("The buffer size was too small. Create a larger buffer and try again.");
        case LA_E_METADATA_KEY_NOT_FOUND: return("A metadata key was not found / does not exist.");
        case LA_E_FEATURE_FLAG_NOT_FOUND: return("No such feature flag found.");
        case LA_E_SYSTEM_PERMISSION: return("Insufficient system permission. Temporarily start your process as an admin user.");
        case LA_E_FILE_PERMISSION: return("No permission to write to a file.");
        case LA_E_INVALID_PERMISSION_FLAG: return("Invalid permission flag.");
        case LA_E_VM: return("The function failed because this instance of your program is running inside a virtual machine / hypervisor, which is not allowed.");
        case LA_E_CONTAINER: return("The function failed because this instance of your program is running inside a container, which is not allowed.");
        case LA_E_COUNTRY: return("Using this license from this country is not allowed.");
        case LA_E_IP: return("Using this license from this IP address is not allowed.");
        case LA_E_DEACTIVATION_LIMIT: return("This product key had a limited number of allowed deactivations. No more deactivations are allowed for the product key.");
        case LA_TRIAL_EXPIRED: return("The trial has expired.");
        case LA_E_TRIAL_ACTIVATION_LIMIT: return("No more verified free trials are available at the moment.");
        case LA_E_USERS_LIMIT_REACHED: return("The user limit for this account has been reached.");
        case LA_E_WMIC: return("The WMI service on this computer is disabled, can't create machine fingerprint.");
        case LA_E_MACHINE_FINGERPRINT: return("The machine fingerprint has changed due to some hardware change. Reactivate your license manually.");
        case LA_GRACE_PERIOD_OVER: return("The grace period for server sync is over.");
        case LA_E_RELEASE_VERSION_NOT_ALLOWED: return("This release of Psychtoolbox is not allowed for use with the current license.");
        case LA_E_OS_USER: return("The operating system user has changed since activation and the license is user-locked.");
        default:
            sprintf(errorCodeString, "Error code: %i.", hr);
            return(errorCodeString);
    }
}

#if PSYCH_SYSTEM == PSYCH_WINDOWS
static void setenv(const char* var, const char* val, int dummy)
{
    (void) dummy;
    char envbuf[32];
    snprintf(envbuf, sizeof(envbuf), "%s=%s", var, val);
    _putenv(envbuf);
}
#endif

static const char* ConvertToChar(TCHAR* inString)
{
#if PSYCH_SYSTEM == PSYCH_WINDOWS
    static char zeroString = 0;
    char* outString = &zeroString;

    int rc = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, inString, -1, NULL, 0, NULL, NULL);
    if (rc <= 0)
        return(outString);

    outString = (char*) PsychCallocTemp(rc, sizeof(char));
    if ((rc > WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, inString, -1, outString, rc, NULL, NULL)) && lmdebug)
        printf("PTB-ERROR: WideCharToMultiByte failed! Less than %i Bytes converted!\n", rc);

    return(outString);
#else
    return((char*) inString);
#endif
}

static TCHAR* ConvertToTCHAR(char* inString)
{
#if PSYCH_SYSTEM == PSYCH_WINDOWS
    // On MS-Windows, need to convert ASCII string to unicode wide char aka TCHAR:
    static TCHAR zeroTString = 0;
    TCHAR* outString = &zeroTString;

    int rc = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inString, -1, NULL, 0);
    if (rc <= 0)
        return(outString);

    outString = (TCHAR*) PsychCallocTemp(rc, sizeof(TCHAR));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inString, -1, outString, rc) <= 0) {
        outString = &zeroTString;
        rc = GetLastError();

        if (lmdebug)
            printf("PTB-ERROR: MultiByteToWideChar failed! Error code: %i.\n", rc);
    }

    return(outString);
#else
    return((TCHAR*) inString);
#endif
}

static const char* DoGetLicenseMetaData(const char* key)
{
    static TCHAR value[512] = { 0 };

    int hr = GetLicenseMetadata(ConvertToTCHAR((char*) key), value, sizeof(value));
    if (hr != LA_OK) {
        if (lmdebug)
            printf("PTB-DEBUG: GetLicenseMetadata('%s') failed. Error %i [%s].\n", key, hr, LMErrorString(hr));
    }

    return(ConvertToChar(value));
}

static psych_bool IsMinimumVersionSatisfied(void)
{
    const char* versionStr = DoGetLicenseMetaData("minVersionRequired");
    int major, minor, point;
    if (3 != sscanf(versionStr, "%i.%i.%i", &major, &minor, &point)) {
        if (lmdebug)
            printf("PTB-DEBUG: IsMinimumVersionSatisfied() version '%s' parse failed. Returning false.\n", versionStr);

        return(FALSE);
    }

    if (lmdebug)
        printf("PTB-DEBUG: IsMinimumVersionSatisfied() version '%s' -> Parses to minimum required %i.%i.%i vs. actual %i.%i.%i.\n",
               versionStr, major, minor, point, PsychGetMajorVersionNumber(), PsychGetMinorVersionNumber(), PsychGetPointVersionNumber());

    if (PsychGetMajorVersionNumber() < major)
        return(FALSE);

    if (PsychGetMajorVersionNumber() > major)
        return(TRUE);

    if (PsychGetMinorVersionNumber() < minor)
        return(FALSE);

    if (PsychGetMinorVersionNumber() > minor)
        return(TRUE);

    if (PsychGetPointVersionNumber() < point)
        return(FALSE);

    return(TRUE);
}

static psych_bool GetFeatureEnabled(const char* featureName)
{
    TCHAR featureValue[128] = { 0 };
    uint32_t enabled = 0;

    int hr = GetProductVersionFeatureFlag(ConvertToTCHAR((char*) featureName), &enabled, featureValue, sizeof(featureValue));
    if (hr != LA_OK) {
        if (lmdebug)
            printf("PTB-DEBUG: GetFeatureValue('%s') failed. Error %i [%s]. Returning false.\n", featureName, hr, LMErrorString(hr));
    }

    return(enabled);
}

static const char* GetSupportToken(void)
{
    static TCHAR activationId[512] = { 0 };
    int hr;

    // Only generate non-empty support token if this license enables user support:
    if (GetFeatureEnabled("UserSupport")) {
        // Use activation id as support token:
        hr = GetActivationId(activationId, sizeof(activationId));
        if (hr != LA_OK) {
            printf("PTB-ERROR: Failed to fetch activation id for support token. Error: %s\n", LMErrorString(hr));
        }

        // TODO: Do a token that is public activation id + SHA256(public activation id + secret license key),
        // like in our old support membership scheme?
    }

    return(ConvertToChar(activationId));
}

static int DoActivateLicense(void)
{
    int hr, frc;
    psych_uint64 maxGracePeriodDays;
    TCHAR keybuf[128] = { 0 };
    time_t endOfGracePeriod = 0;
    time_t now = 0;

    // Mark at least locally as inactive:
    SetActivationMetadata(_T("ReallyActive"), _T("0"));

    // Try to activate license online:
    hr = ActivateLicense();
    if (lmdebug)
        printf("PTB-DEBUG: ActivateLicense() = %i [%s]\n", hr, LMErrorString(hr));

    // Failed for some reason? Bail, returning error code to caller:
    if (hr != LA_OK)
        return(hr);

    // Activated. Check if the currently set server sync grace period is within what
    // this release considers ok:
    hr = GetServerSyncGracePeriodExpiryDate((uint32_t*) &endOfGracePeriod);
    if (lmdebug)
        printf("PTB-DEBUG: GetServerSyncGracePeriodExpiryDate() = %i [%s]\n", hr, LMErrorString(hr));

    if (hr == LA_OK) {
        // Got grace period expiry date. Check if it is acceptable to us:
        now = time(NULL);

        // We do not allow "no grace period" / "infinite grace period", iow. we
        // do not allow machines to run offline without server sync ever:
        if ((endOfGracePeriod > 0) && (now > 0) && (endOfGracePeriod > now)) {
            // Has a finite grace period. Compute its length:
            maxGracePeriodDays = (endOfGracePeriod - now) / 86400;
            if (lmdebug)
                printf("PTB-DEBUG: maxGracePeriodDays %i.\n", maxGracePeriodDays);

            // We only accept grace periods of up to 30 days for now:
            if (maxGracePeriodDays <= 30) {
                // Ok, we got a valid grace period that is no longer than 30 days. This is acceptable.
                if (lmdebug)
                    printf("PTB-DEBUG: maxGracePeriodDays %i within max 30 days bound. All good, activated.\n", maxGracePeriodDays);

                // Mark at least locally as active:
                SetActivationMetadata(_T("ReallyActive"), _T("1"));

                // Sync ReallyActive to servers:
                ActivateLicense();

                return(LA_OK);
            }
            else {
                printf("PTB-ERROR: The license for this machine does have a grace period of %i days, larger than acceptable 30 days! This is forbidden!\n", maxGracePeriodDays);
                printf("PTB-ERROR: Not activating on this machine. Contact support for assistance.\n");
            }
        }
        else {
            // This one has no grace period. Forbidden!
            printf("PTB-ERROR: The license for this machine does not have a valid finite grace period (%i), or now check failed (%i)!\n",
                   endOfGracePeriod, now);
            printf("PTB-ERROR: This is forbidden! Not activating on this machine. Contact support for assistance.\n");
        }
    }
    else {
        printf("PTB-ERROR: Failed to get grace period! Not activating on this machine. Contact support for assistance.\n");
    }

    // Failed to get an activated license with acceptable parameters. Deactivate
    // license again, while retaining a potentially saved license key:
    frc = GetLicenseKey(keybuf, sizeof(keybuf));

    // Deactivate:
    hr = DeactivateLicense();
    if (lmdebug)
        printf("PTB-DEBUG: DeactivateLicense() = %i [%s]\n", hr, LMErrorString(hr));

    // Restore previously backed up key if possible:
    if (frc == LA_OK)
        SetLicenseKey(keybuf);

    // Signal a problem with the grace period:
    return(LA_FAIL);
}

static int PTBIsLicenseGenuine(void)
{
    int hr;
    time_t endOfGracePeriod = 0;
    TCHAR reallyActive[2];

    // Do the real check, offline or online, depending on server sync status:
    hr = IsLicenseGenuine();
    if (lmdebug)
        printf("PTB-DEBUG: Real IsLicenseGenuine() = %i [%s]\n", hr, LMErrorString(hr));

    // Failed for some reason -> Game over:
    if (hr != LA_OK)
        return(hr);

    // Double-check the grace period is not 0 == none / infinite:
    hr = GetServerSyncGracePeriodExpiryDate((uint32_t*) &endOfGracePeriod);
    if (lmdebug)
        printf("PTB-DEBUG: GetServerSyncGracePeriodExpiryDate(%i) = %i [%s]\n", (int) endOfGracePeriod, hr, LMErrorString(hr));

    if (hr != LA_OK)
        return(hr);

    if (endOfGracePeriod == 0)
        return(LA_FAIL);

    // Worked. Make sure our special extra "Ok" conditions from DoActivateLicense() are met:
    hr = GetActivationMetadata(_T("ReallyActive"), reallyActive, sizeof(reallyActive));
    if (lmdebug)
        printf("PTB-DEBUG: GetActivationMetadata() = %i [%s]\n", hr, LMErrorString(hr));

    // Failed for some reason -> Game over:
    if (hr != LA_OK)
        return(hr);

    // Detect if we have marked ourselves as really activated or not:
    hr = (!strcmp(ConvertToChar(reallyActive), "1")) ? LA_OK : LA_FAIL;
    if (lmdebug)
        printf("PTB-DEBUG: reallyActive status = %i [%s]\n", hr, LMErrorString(hr));

    return(hr);
}

// Check if one-time print of status messages in this session should be done.
static psych_bool ShouldPrint(void)
{
    const char* e = getenv("PSYCH_LM_PRINTINGDONE");
    if (!e || !strlen(e) || !strcmp(e, "0")) {
        // Not yet printed. Return "do print!", and mark as one-time print done:
        setenv("PSYCH_LM_PRINTINGDONE", "1", 1);
        return(TRUE);
    }

    // Already printed, so should not print again:
    return(FALSE);
}

static psych_bool PsychInitLicenseManager(void)
{
    char laDatFilePath[4096] = { 0 };
    char allowFilePath[4096] = { 0 };
    int hr;

    // Early exit if already initialized:
    if (lminitialized)
        return(TRUE);

    // Verbose debug output wrt. license checking/management requested?
    lmdebug = (getenv("PSYCH_LM_DEBUG") != NULL) ? atoi(getenv("PSYCH_LM_DEBUG")) : 0;

    // Check if allow file for license management exists, bail if it doesn't exist:
    snprintf(allowFilePath, sizeof(allowFilePath), "%sLMOpsAllowed.txt", PsychRuntimeGetPsychtoolboxRoot(TRUE));
    FILE* fid = fopen(allowFilePath, "rt");
    if (!fid) {
        if (lmdebug)
            printf("PTB-INFO: License management allow file '%s' failed to open. Game over!\n", allowFilePath);

        printf("PTB-INFO: License management is not yet approved and enabled by user, marking as not activated.\n");
        printf("PTB-INFO: See 'help PsychLicenseHandling' for information on how to enable license management. Bye!\n");

        return(FALSE);
    }

    // License management allowed:
    fclose(fid);

    // Enable writing of network debug logs into file lexactivator-logs.log if debugging is on with flag 2:
    // NOTE: There's a bug in LexActivator's output log writing that redirects stdout to the log file, so
    // regular octave in gui and non-gui mode, and Matlab non-gui output no longer reaches the command window!
    // Only Matlab in gui mode seems unaffected. Therefore we require the 2 flag to enable log writing for now.
    if (lmdebug & 2)
        SetDebugMode(1);

    // Set path to LexActivator product Psychtoolbox.dat file:
    snprintf(laDatFilePath, sizeof(laDatFilePath), "%sPsychBasic/PsychPlugins/Psychtoolbox.dat", PsychRuntimeGetPsychtoolboxRoot(FALSE));
    hr = SetProductFile(ConvertToTCHAR(laDatFilePath));
    if (lmdebug)
        printf("PTB-DEBUG: SetProductFile() = %i [%s]\n", hr, LMErrorString(hr));

    if (hr != LA_OK) {
        printf("PTB-ERROR: Failed to find or load the product dat file. Error code %i [%s].\n", hr, LMErrorString(hr));
        printf("PTB-ERROR: Make sure that the product dat file is found under the following path and filename:\n");
        printf("PTB-ERROR: %s\n", laDatFilePath);
        PsychErrorExitMsg(PsychError_system, "License manager init failed: Failed to find and load product file.");
    }

    // Set product id for LexActivator function calls: TODO use LA_USER instead, or on non-Windows?
    hr = SetProductId(_T("d616be88-af4b-4088-9190-cf17da37da7b"), LA_ALL_USERS);

    if (hr != LA_OK) {
        printf("PTB-ERROR: Failed to set the product id. Make sure the product id is correct,\n");
        printf("PTB-ERROR: and that the product dat file is found under the following path:\n");
        printf("PTB-ERROR: %s\n", laDatFilePath);
        printf("PTB-ERROR: Error code %i [%s].\n", hr, LMErrorString(hr));
        PsychErrorExitMsg(PsychError_system, "License manager init failed: Failed to set product id.");
    }

    // Allow override of network proxy URL for license management. By default, the operating systems default proxy setting is used:
    if (getenv("PSYCH_LM_NETWORKPROXYURL")) {
        char* proxyURL = getenv("PSYCH_LM_NETWORKPROXYURL");
        hr = SetNetworkProxy(ConvertToTCHAR(proxyURL));
        if (hr != LA_OK) {
            printf("PTB-ERROR: Failed to set override network proxy URL for license management to '%s'. Error: %s\n",
                   proxyURL, LMErrorString(hr));
            printf("PTB-ERROR: License checking may fail and Psychtoolbox may be disabled until you fix this.\n");
        }
        else if (lmdebug) {
            printf("PTB-DEBUG: Set override network proxy URL for license management to '%s'.\n", proxyURL);
        }
    }

    // Allow override of license server URL for license management. This is future-proofing for the unlikely case we'd
    // ever switch to our own on-premises hosting solution. By default, LexActivators builtin address of the Cryptlex SaaS
    // api servers is used:
    if (getenv("PSYCH_LM_LICENSESERVERURL")) {
        char* serverURL = getenv("PSYCH_LM_LICENSESERVERURL");
        hr = SetCryptlexHost(ConvertToTCHAR(serverURL));
        if (hr != LA_OK) {
            printf("PTB-ERROR: Failed to set license server URL for license management to '%s'. Error: %s\n",
                   serverURL, LMErrorString(hr));
            printf("PTB-ERROR: License checking may fail and Psychtoolbox may be disabled until you fix this.\n");
        }
        else if (lmdebug) {
            printf("PTB-DEBUG: Set license server URL to '%s'.\n", serverURL);
        }
    }

    // Define activation options used for (re-)activating a license on this machine. This allows to store an
    // up to 4096 UTF-8 character string per 256 character key of information on the servers for each activation.
    // We store operating system type, runtime type, and machine architecture.
    snprintf(activationMetaDataString, 255, "%s",
             PSYCHTOOLBOX_OS_NAME " " PSYCHTOOLBOX_SCRIPTING_LANGUAGE_NAME " " PTB_ARCHITECTURE " " PTB_ISA);
    SetActivationMetadata(_T("PTBInfo"), ConvertToTCHAR(activationMetaDataString));
    SetTrialActivationMetadata(_T("PTBInfo"), ConvertToTCHAR(activationMetaDataString));

    // Deprecated according to forum. Apparently SetReleaseVersion() is what one shall use, as we do.
    SetAppVersion(_T("Standard"));

    snprintf(activationMetaDataString, 255, "%i.%i.%i", PsychGetMajorVersionNumber(), PsychGetMinorVersionNumber(), PsychGetPointVersionNumber());
    SetReleaseVersion(ConvertToTCHAR(activationMetaDataString));

    //SetReleasePublishedDate();
    SetReleasePlatform(_T(PSYCHTOOLBOX_OS_NAME " " PSYCHTOOLBOX_SCRIPTING_LANGUAGE_NAME " " PTB_ARCHITECTURE " " PTB_ISA));

    SetReleaseChannel(_T("stable"));

    // Lock libLexActivator permanently into the host process space, so that it stays
    // put even if all PTB mex files get flushed. Why? Because the library will launch
    // a license server network sync background thread, which sleeps most of the time,
    // but wakes up once per server sync interval. Turns out that if out mex files get unloaded,
    // then normally the LexActivator library gets unloaded, as its refcount of users drops to
    // zero, unmapping the library code of the sync thread. When the OS sleep function returns,
    // it has nothing to return to and boom - segfault! Prevent this by preventing LexActivator
    // from ever unloading during the host process lifetime:
    #if PSYCH_SYSTEM == PSYCH_WINDOWS
    if (!LoadLibrary(_T("LexActivator.dll")))
    #elif PSYCH_SYSTEM == PSYCH_OSX
    if (!dlopen("libLexActivator.dylib", RTLD_LAZY | RTLD_NODELETE))
    #elif PSYCH_SYSTEM == PSYCH_LINUX
    if (!dlopen("libLexActivator.so", RTLD_LAZY | RTLD_NODELETE))
    #endif
        printf("PTB-ERROR: LM library failed to lock into process!\n");

    // Mark as initialized:
    lminitialized = TRUE;

    return(lminitialized);
}

static psych_bool PsychCheckLicenseStatus(void)
{
    int hr;

    // Init if not initialized:
    if (!PsychInitLicenseManager())
        return(FALSE);

    // Check license 1st time. This will usually succeed on a valid, active, non-expired license,
    // and also start the background sync thread to update to the latest info from the license
    // servers, if an internet connection is available. It will also trigger periodic rechecks in
    // the background:
    hr = PTBIsLicenseGenuine();
    if (lmdebug)
        printf("PTB-DEBUG: 1st PTBIsLicenseGenuine() = %i [%s]\n", hr, LMErrorString(hr));

    // No valid usable license? Could be that is the case, ie. invalid, suspended, revoked or
    // expired license, or online check grace period over. However, the previous call will have
    // started an attempt at an online sync, which may change the situation, based on new data
    // from the license servers. Therefore, if 1st check failed to signal a valid license, try
    // again after 10 seconds, after which an online sync may have changed the situation if an
    // internet connection was possible. We skip the 2nd check after 10 seconds if LA_FAIL is
    // the real return code of the real license query - in this case we can fast-forward to the
    // free trial activation and use:
    if ((hr != LA_OK) && (IsLicenseGenuine() != LA_FAIL)) {
        // Recheck license after a 10 second wait to give a potential server sync a chance:
        PsychYieldIntervalSeconds(10);
        hr = PTBIsLicenseGenuine();
        if (lmdebug)
            printf("PTB-DEBUG: After 10 seconds, 2nd PTBIsLicenseGenuine() = %i [%s]\n", hr, LMErrorString(hr));
    }

    // License totally good, after (possibly repeated) check?
    if (hr == LA_OK) {
        // License is locally activated, not suspended, not expired, and last server sync either
        // happened in time or we are still inside the offline grace period between server syncs.
        int offlineDaysRemaining, licenseDaysRemaining;
        time_t licenseExpiryDate = 0;
        time_t endOfGracePeriod = 0;
        GetLicenseExpiryDate((uint32_t*) &licenseExpiryDate);
        licenseDaysRemaining = (licenseExpiryDate - time(NULL)) / 86400;
        GetServerSyncGracePeriodExpiryDate((uint32_t*) &endOfGracePeriod);
        offlineDaysRemaining = (endOfGracePeriod - time(NULL)) / 86400;

        if (lmdebug)
            printf("PTB-DEBUG: licenseDaysRemaining %i, offlineDaysRemaining %i.\n", licenseDaysRemaining, offlineDaysRemaining);

        // License is valid, activated for this machine, and not expired.

        // Disallow use of this license if the current Psychtoolbox version is too old for it:
        if (!IsMinimumVersionSatisfied()) {
            printf("PTB-ERROR: This Psychtoolbox version is too old for the currently active license. Upgrade your Psychtoolbox!\n");
            return(FALSE);
        }

        // Disallow use with Matlab if "NoMatlab" feature flag is set as true:
        #ifndef PTBOCTAVE3MEX
        if ((PSYCH_LANGUAGE == PSYCH_MATLAB) && GetFeatureEnabled("NoMatlab")) {
            printf("PTB-ERROR: Use of Psychtoolbox with Matlab is not allowed by the currently active license. Use GNU/Octave instead.\n");
            return(FALSE);
        }
        #endif

        // Print one-time status update about positive licensing status and offline time remaining?
        if (ShouldPrint()) {
            printf("PTB-INFO: Psychtoolbox license is active on this machine for %i more days until %s",
                   licenseDaysRemaining, ctime(&licenseExpiryDate));

            if (offlineDaysRemaining <= licenseDaysRemaining)
                printf("PTB-INFO: Up to %i more days of offline use without internet connection are possible until %s",
                       offlineDaysRemaining, ctime(&endOfGracePeriod));

            if ((offlineDaysRemaining < 2) && (offlineDaysRemaining < licenseDaysRemaining))
                printf("PTB-INFO: CAUTION! You need to connect this machine to the internet within less than %i days to keep this Psychtoolbox working.\n",
                       offlineDaysRemaining + 1);

            if (licenseDaysRemaining < 10)
                printf("PTB-INFO: CAUTION! You need to renew your license, and then connect to the internet, within less than %i days to keep the license active.\n",
                       licenseDaysRemaining + 1);

            printf("PTB-INFO: The support authentication token would be: %s\n\n", GetSupportToken());
        }

        return(TRUE);
    }
    else if (hr == LA_EXPIRED) {
        // Expired! Fail the license check:
        time_t licenseExpiryDate = 0;
        GetLicenseExpiryDate((uint32_t*) &licenseExpiryDate);

        printf("PTB-INFO: The current license for this machine has expired on %s", ctime(&licenseExpiryDate));
        printf("PTB-INFO: You need to renew your license or buy a new license to use Psychtoolbox again.\n");
        printf("PTB-INFO: See 'help PsychLicenseHandling' for information on how to do that.\n");
        printf("PTB-INFO: If you already renewed your license, then my knowledge of it might not yet be\n");
        printf("PTB-INFO: up to date, so I need to refresh my data online. Run PsychLicenseHandling('Activate') to do that.\n");

        return(FALSE);
    }
    else if (hr == LA_SUSPENDED) {
        // Suspended! Fail the license check:
        printf("PTB-INFO: The current license for this machine has been suspended for some reason, e.g.,\n");
        printf("PTB-INFO: you canceled your purchase of the license, or your payment did not yet come through.\n");
        printf("PTB-INFO: You need to fix whatever caused suspension of the license to use Psychtoolbox again.\n");
        printf("PTB-INFO: See 'help PsychLicenseHandling' for information on how to do that.\n");
        printf("PTB-INFO: If you have already fixed your license, then my knowledge of it might not yet be\n");
        printf("PTB-INFO: up to date, so you need to refresh my data online. Run PsychLicenseHandling('Activate') to do that.\n");

        return(FALSE);
    }
    else if (hr == LA_GRACE_PERIOD_OVER) {
        // Is the license offline activated on this machine, but we couldn't connect to
        // the license servers for reverification within the checkperiod + grace period?

        // There is still activation data on the computer, and it's valid.
        // This means connections to the activation servers were blocked (intentionally or not)
        // for longer than the grace period allows.
        unsigned int cnt;
        time_t endOfGracePeriod = 0;
        GetServerSyncGracePeriodExpiryDate((uint32_t*) &endOfGracePeriod);

        printf("PTB-INFO: This license is still activated, but the deadline for required license online checks of %s", ctime(&endOfGracePeriod));
        printf("PTB-INFO: has passed. I must reverify with the activation servers online now, before you can use Psychtoolbox again.\n");
        printf("PTB-INFO: I will now try 3 times, with a 20 seconds pause between each try, to contact the license servers to reverify.\n");
        printf("PTB-INFO: Please connect your machine to the internet now and do not block access to the license servers (firewall etc.).\n");

        for (cnt = 1; cnt <= 3; cnt++) {
            printf("PTB-INFO: Try %i in 20 seconds... ", cnt);
            #ifndef PTBOCTAVE3MEX
            PsychRuntimeEvaluateString("pause(0.001)");
            #endif

            PsychYieldIntervalSeconds(20);

            printf("... now!\n");
            #ifndef PTBOCTAVE3MEX
            PsychRuntimeEvaluateString("pause(0.001)");
            #endif

            // Force a server sync right now, even if server sync period is not yet reached,
            // so we get back into the grace period if possible to clear the error that brought
            // us here in the first place:
            DoActivateLicense();

            // Recheck with up to date license info if we are now good again:
            hr = PTBIsLicenseGenuine();
            if (lmdebug)
                printf("PTB-DEBUG: PTBIsLicenseGenuine() = %i [%s]\n", hr, LMErrorString(hr));

            if (hr == LA_OK) {
                GetServerSyncGracePeriodExpiryDate((uint32_t*) &endOfGracePeriod);
                printf("PTB-INFO: Successfully verified with the servers. Latest possible time of next online reverification\n");
                printf("PTB-INFO: will be in %i days on %s", (endOfGracePeriod - time(NULL)) / 86400, ctime(&endOfGracePeriod));

                // Recheck license status through conventional path and return the result:
                return(PsychCheckLicenseStatus());
            }
            else {
                printf("PTB-INFO: Failed to verify with the servers: %s. Make sure you are connected to the internet.\n",
                       LMErrorString(hr));
            }
        }

        printf("\nPTB-INFO: Type 'clear all', make sure to enable an internet connection, and then try again. Psychtoolbox is disabled until then.\n");

        // Game over for now:
        return(FALSE);
    }
    else {
        // Ok, no conventional license is available, valid and active. Maybe the user is
        // eligible for a time limited trial?

        // Start or re-validate the trial if it has already started:
        hr = IsTrialGenuine();
        if (lmdebug)
            printf("PTB-DEBUG: IsTrialGenuine() = %i [%s]\n", hr, LMErrorString(hr));

        if (hr != LA_OK) {
            hr = ActivateTrial();
            if (lmdebug)
                printf("PTB-DEBUG: ActivateTrial() = %i [%s]\n", hr, LMErrorString(hr));
        }

        if (hr == LA_OK) {
            TCHAR trialId[512];
            time_t trialExpiryDate = 0;
            int trialDays;

            // Get the end date of the trial and the number of trial days remaining:
            GetTrialExpiryDate((uint32_t*) &trialExpiryDate);
            trialDays = (trialExpiryDate - time(NULL)) / 86400;

            // Get unique trial id string:
            GetTrialId(trialId, sizeof(trialId));

            if (ShouldPrint()) {
                printf("PTB-INFO: Your free trial is active for %d more days, until %s", trialDays, ctime(&trialExpiryDate));
                printf("PTB-INFO: After that date, you will have to buy a license to continue using Psychtoolbox on this machine.\n");
                printf("PTB-INFO: The unique id of this trial is: %s\n", ConvertToChar(trialId));
            }

            return(TRUE);
        }
        else {
            printf("PTB-INFO: No free trial eligibility: %s\n", LMErrorString(hr));

            // Try one last regular real server license check, so we get the error code of failure:
            hr = IsLicenseGenuine();
            if (lmdebug)
                printf("PTB-DEBUG: Final post-trial IsLicenseGenuine() = %i [%s]\n", hr, LMErrorString(hr));

            switch (hr) {
                // All these have already be handled by code above/before trial handling, no need to rub it in:
                case LA_OK: // fallthrough
                case LA_EXPIRED: // fallthrough
                case LA_SUSPENDED: // fallthrough
                case LA_GRACE_PERIOD_OVER: // fallthrough
                case LA_FAIL: // fallthrough
                break;

                // Unhandled failure condition. Tell user what went wrong:
                default:
                    printf("PTB-INFO: A regular non-trial license does not work for the following reason that you need to fix:\n");
                    printf("PTB-INFO: %s\n\n", LMErrorString(hr));
            }
        }

        // Activation or revalidation of free trial failed.
        printf("PTB-INFO: There isn't any license active for Psychtoolbox on this machine and operating system,\n");
        printf("PTB-INFO: and you are not, or no longer, eligible for a free trial. You need to buy a paid license\n");
        printf("PTB-INFO: to use Psychtoolbox. See 'help PsychLicenseHandling' for information on how to do that. Bye!\n");

        return(FALSE);
    }

    printf("PTB-CRITICAL: This LM path should never be reached!\n");

    return(FALSE);
}

psych_bool PsychIsLicensed(const char* featureName)
{
    // Licensing status unknown? Perform active check to determine it:
    if (licenseStatus == -1) {
        // License generally enabled, valid and active for this machine, or an active trial?
        licenseStatus = PsychCheckLicenseStatus();
    }

    // If enabled state of a specific feature is requested, return that:
    if (featureName)
        return(GetFeatureEnabled(featureName));

    // Return general licensing status, cached or just determined:
    return((licenseStatus == 1) ? TRUE : FALSE);
}

PsychError PsychManageLicense(void)
{
    static char useString[] = "returnValue = Modulename('ManageLicense' [, mode=0][, productKey][, dataString]);";
    static char synopsisString[] =
        "Return info about, or change licensing status of Psychtoolbox on this machine.\n"
        "'mode' Optional parameter to request a specific action. Default is zero.\n"
        " 0 = Query current licensing status: Returns 1 if license active, 0 otherwise.\n"
        "     Query per-feature status if a feature name is specified as 2nd argument.\n"
        "\n"
        " The following functions return zero for success, or a non-zero error code:\n"
        "-1 = Deactivate license on this machine for now.\n"
        "-2 = Deactivate license on this machine and remove enrolled product key.\n"
        "+1 = Activate license on this machine, based on enrolled productKey.\n"
        "+2 = Activate license on this machine with a new 'productKey'.\n"
        "+3 = Check if a valid product key is already enrolled and available. 0 = Yes.\n"
        "+4 = Get an up to date support authentication token.\n"
        "+5 = Set activation metadata property (2nd argument) to the dataString in 3rd argument.\n"
        "+6 = Return 1 on a license managed Psychtoolbox, 0 otherwise.\n";
    static char seeAlsoString[] = "";

    int rc = 0;
    int mode = 0;
    char *productKey = NULL;
    char *idString = NULL;
    int64_t maxActivations;
    uint32_t curActivations;

    // All subfunctions should have these two lines:
    PsychPushHelp(useString, synopsisString, seeAlsoString);
    if (PsychIsGiveHelp()) { PsychGiveHelp(); return(PsychError_none); };

    // Check for valid number of arguments:
    PsychErrorExit(PsychCapNumInputArgs(3));
    PsychErrorExit(PsychCapNumOutputArgs(1));

    // Get optional argument 'mode':
    PsychCopyInIntegerArg(1, kPsychArgOptional, &mode);

    // Return if license management is build-time supported:
    if (mode == 6) {
        // This is always 1 = true if we get here, as oppposed to the non-LM stub that always returns 0:
        rc = 1;
        goto licensemanager_out;
    }

    // Get optional argument 'productKey' for activation:
    PsychAllocInCharArg(2, kPsychArgOptional, &productKey);

    // Get optional argument 'idString' for activation user data:
    if (PsychAllocInCharArg(3, kPsychArgOptional, &idString) && strlen(idString) > 30) {
        // User provided arbitrary idString gets truncated to at most 30 characters + zero terminator:
        idString[30] = 0;
    }

    // Init license manager if not already initialized:
    if (!PsychInitLicenseManager()) {
        // Init likely failed because allow file does not exist:
        rc = -1;
        goto licensemanager_out;
    }

    switch (mode) {
        case 0: // Check licensing status:
            rc = PsychIsLicensed(productKey);
            break;

        case -1: // Deactivate license on this machine:
            {
                GetLicenseAllowedActivations(&maxActivations);
                GetLicenseTotalActivations(&curActivations);

                // Deactivating a license on a machine erases the enrolled license key.
                // As a way to retain the key, do a backup key -> deactivate -> restore key:
                TCHAR keybuf[128] = { 0 };
                int frc = GetLicenseKey(keybuf, sizeof(keybuf));

                // Mark at least locally as inactive:
                SetActivationMetadata(_T("ReallyActive"), _T("0"));

                // Deactivate:
                rc = DeactivateLicense();

                // Restore previously backed up key if possible:
                if (frc == LA_OK)
                    SetLicenseKey(keybuf);

                licenseStatus = -1;
                if (rc != LA_OK) {
                    printf("PTB-ERROR: License deactivation failed: %s\n", LMErrorString(rc));
                }
                else {
                    printf("PTB-INFO: License deactivated on this machine. Product key kept for easy reactivation.\n");
                    printf("PTB-INFO: Now %i out of a maximum of %li activations for this license remain in use.\n", curActivations - 1, maxActivations);
                }
            }
            break;

        case -2: // Deactivate license on this machine and delete product key:
            GetLicenseAllowedActivations(&maxActivations);
            GetLicenseTotalActivations(&curActivations);

            // Mark at least locally as inactive:
            SetActivationMetadata(_T("ReallyActive"), _T("0"));

            rc = DeactivateLicense();
            licenseStatus = -1;
            if (rc != LA_OK) {
                printf("PTB-ERROR: License deactivation with product key erasure failed: %s\n", LMErrorString(rc));
            }
            else {
                printf("PTB-INFO: License deactivated on this machine and old product key deleted from machine.\n");
                printf("PTB-INFO: Now %i out of a maximum of %li activations for this license remain in use.\n", curActivations - 1, maxActivations);
            }

            break;

        case 1: // Activate license on this machine with already stored product key:
            // fallthrough
        case 2: // Activate license on this machine with provided new product key:
            if (mode == 2) {
                // Activate with new provided product key:
                if (!productKey) {
                    // No product key provided as parameter:
                    printf("PTB-ERROR: You wanted to activate the machine license with a new product key, but you did not provide a new product key.\n");
                    printf("PTB-ERROR: Could not activate license, no new product key provided.\n");
                    rc = LA_FAIL;
                    goto licensemanager_out;
                }

                // Check product key for validity, then save it for use by all other functions:
                TCHAR* unicodeProductKey = ConvertToTCHAR(productKey);
                if (!unicodeProductKey) {
                    printf("PTB-ERROR: You wanted to activate the machine license, but the provided product key '%s' could not be converted to wide-char.\n", productKey);
                    goto licensemanager_out;
                }
                else {
                    // Converted to TCHAR string. Try to validate and save product key:
                    rc = SetLicenseKey(unicodeProductKey);
                }

                if (rc != LA_OK) {
                    printf("PTB-ERROR: You wanted to activate the machine license with a new product key, but the provided product key could not be saved:\n");
                    printf("PTB-ERROR: %s\n", LMErrorString(rc));
                    printf("PTB-ERROR: Could not activate license, as product key is redundant, invalid or could not be saved.\n");
                    goto licensemanager_out;
                }
            }
            else {
                // Activate with existing product key:
                TCHAR keybuf[128] = { 0 };
                rc = GetLicenseKey(keybuf, sizeof(keybuf));
                if ((rc == LA_OK) && (strlen(ConvertToChar(keybuf)) > 0)) {
                    printf("PTB-INFO: Trying to activate license on this machine with currently enrolled product key.\n");
                }
                else {
                    if (rc == LA_OK)
                        rc = LA_FAIL;

                    printf("PTB-ERROR: Failed to validate product key: %s\n", LMErrorString(rc));
                    printf("PTB-ERROR: Could not activate license, as no product key is enrolled, or the key is invalid.\n");
                    goto licensemanager_out;
                }
            }

            // If we made it up to here, then we should have a valid product key enrolled, so try to activate
            // machine under this license:
            setenv("PSYCH_LM_PRINTINGDONE", "", 1);
            licenseStatus = -1;

            // Set optional user provided idString:
            if (idString) {
                rc = SetActivationMetadata(_T("IdString"), ConvertToTCHAR(idString));
                if (rc != LA_OK)
                    printf("PTB-WARNING: Could not store the optional idString you provided. Will activate license anyway, without that info: %s\n",
                           LMErrorString(rc));
            }

            // Activate license:
            rc = DoActivateLicense();
            if (rc == LA_OK) {
                GetLicenseAllowedActivations(&maxActivations);
                GetLicenseTotalActivations(&curActivations);
                printf("PTB-INFO: Current product key activated successfully. Now %i out of a maximum of %li activations for this license are in use.\n",
                       curActivations, maxActivations);
                licenseStatus = PsychCheckLicenseStatus();
            }
            else {
                printf("PTB-ERROR: License activation failed: %s\n", LMErrorString(rc));
                if (rc == LA_E_ACTIVATION_LIMIT) {
                    printf("PTB-INFO: You need to deactivate the same license on another operating-system + machine combination first,\n");
                    printf("PTB-INFO: before you can activate it on this machine. See 'help PsychLicenseHandling' on how to do that.\n");
                    printf("PTB-INFO: Alternatively you could buy another license.\n");
                }

                if (rc == LA_E_RELEASE_VERSION_NOT_ALLOWED) {
                    printf("PTB-INFO: Your current Psychtoolbox %s is too recent for this license.\n", PsychGetVersionString());
                }
            }
            break;

        case 3: // Check if product key is already stored and valid:
            {
                // Retrieve key:
                TCHAR keybuf[1024] = { 0 };
                rc = GetLicenseKey(keybuf, sizeof(keybuf));
                // A length zero key maps to failure, ie. LA_E_LICENSE_KEY:
                if ((rc == LA_OK) && (strlen(ConvertToChar(keybuf)) == 0))
                    rc = LA_E_LICENSE_KEY;
            }
            break;

        case 4: // Get an up to date support authentication token:
            {
                time_t syncTime;
                rc = ActivateLicense();
                syncTime = time(NULL);

                if (lmdebug)
                    printf("PTB-DEBUG: ActivateLicense() = %i [%s].\n", rc, LMErrorString(rc));

                printf("PTB-INFO: The support authentication token is: %s - Synced at %s",
                       GetSupportToken(), (syncTime != -1 && rc == LA_OK) ? ctime(&syncTime) : "unknown time.\n");
            }
            break;

        case 5: // Set an optional key->value pair of activation metadata:
            {
                if (productKey && (!strcmp(productKey, "hostappversion") || !strcmp(productKey, "machinemodel") || !strcmp(productKey, "gpumodel"))) {
                    // Store per activation metadata:
                    if (idString) {
                        // Trial activation data:
                        SetTrialActivationMetadata(ConvertToTCHAR(productKey), ConvertToTCHAR(idString));

                        // Activation data:
                        rc = SetActivationMetadata(ConvertToTCHAR(productKey), ConvertToTCHAR(idString));
                        if (rc != LA_OK) {
                            printf("PTB-WARNING: Could not store the optional activation metadata '%s' -> '%s' you provided. Not a big deal. Error: %s\n",
                                   productKey, idString, LMErrorString(rc));
                        }
                        else if (lmdebug) {
                            printf("PTB-DEBUG: Locally set activation metadata '%s' -> '%s'.\n", productKey, idString);
                        }
                    }
                    else {
                        // Clear trial activation data:
                        SetTrialActivationMetadata(ConvertToTCHAR(productKey), _T(""));

                        // Clear activation data:
                        rc = SetActivationMetadata(ConvertToTCHAR(productKey), _T(""));
                        if (rc != LA_OK) {
                            printf("PTB-WARNING: Could not clear the optional activation metadata for key '%s'. Error: %s\n", productKey, LMErrorString(rc));
                        }
                        else if (lmdebug) {
                            printf("PTB-DEBUG: Locally deleted activation metadata '%s'.\n", productKey);
                        }
                    }
                }
                else {
                    printf("PTB-ERROR: Unrecognized / Unauthorized activation metadata key '%s' provided.\n", productKey);
                    PsychErrorExitMsg(PsychError_user, "Tried to set invalid activation metadata key.");
                }
            }
            break;

        case 6: // Return if license management is supported:
            // This is always 1 = true if we get here, as oppposed to the non-LM stub that always returns 0 = false:
            rc = 1;
            break;

        default:
            PsychErrorExitMsg(PsychError_user, "Invalid mode argument provided. Must be in range -2 to +6.");
    }

licensemanager_out:
    PsychCopyOutDoubleArg(1, kPsychArgOptional, (double) rc);

    // Reset global user status message printing to allow to print one-time info related to new state:
    if (licenseStatus == -1)
        setenv("PSYCH_LM_PRINTINGDONE", "", 1);

    return(PsychError_none);
}

// Record for statistical purpose that feature 'featureName' is used in this session:
void PsychFeatureUsed(const char* featureName)
{
    // This doesn't do or record anything so far. Just a stub placeholder for
    // potential future use:
    (void) featureName;
}

#ifdef _UNICODE
#undef _UNICODE
#endif

#ifdef UNICODE
#undef UNICODE
#endif

#endif
