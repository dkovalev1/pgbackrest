/***********************************************************************************************************************************
Configuration Load
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "command/command.h"
#include "command/lock.h"
#include "common/crypto/common.h"
#include "common/debug.h"
#include "common/io/io.h"
#include "common/io/socket/common.h"
#include "common/log.h"
#include "common/memContext.h"
#include "config/config.intern.h"
#include "config/load.h"
#include "config/parse.h"
#include "info/infoBackup.h"
#include "storage/cifs/storage.h"
#include "storage/helper.h"
#include "storage/posix/storage.h"
#include "storage/sftp/storage.h"

/***********************************************************************************************************************************
Local variables
***********************************************************************************************************************************/
static struct ConfigLoadLocal
{
    unsigned int argListSize;                                       // Argument list size
    const char **argList;                                           // Argument list
} configLoadLocal;

/***********************************************************************************************************************************
Load log settings
***********************************************************************************************************************************/
static void
cfgLoadLogSetting(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    // Initialize logging
    LogLevel logLevelConsole = logLevelOff;
    LogLevel logLevelStdErr = logLevelOff;
    LogLevel logLevelFile = logLevelOff;
    bool logTimestamp = true;
    unsigned int logProcessMax = 1;

    if (cfgOptionValid(cfgOptLogLevelConsole))
        logLevelConsole = logLevelEnum(cfgOptionStrId(cfgOptLogLevelConsole));

    if (cfgOptionValid(cfgOptLogLevelStderr))
        logLevelStdErr = logLevelEnum(cfgOptionStrId(cfgOptLogLevelStderr));

    if (cfgOptionValid(cfgOptLogLevelFile))
        logLevelFile = logLevelEnum(cfgOptionStrId(cfgOptLogLevelFile));

    if (cfgOptionValid(cfgOptLogTimestamp))
        logTimestamp = cfgOptionBool(cfgOptLogTimestamp);

    if (cfgOptionValid(cfgOptProcessMax))
        logProcessMax = cfgOptionUInt(cfgOptProcessMax);

    logInit(
        logLevelConsole, logLevelStdErr, logLevelFile, logTimestamp, 0, logProcessMax,
        cfgOptionValid(cfgOptDryRun) && cfgOptionBool(cfgOptDryRun));

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN void
cfgLoadUpdateOption(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    FUNCTION_AUDIT_HELPER();

    // Make sure the repo option is set for the stanza-delete command when more than one repo is configured or the first configured
    // repo is not key 1.
    if (!cfgCommandHelp() && cfgOptionValid(cfgOptRepo) && cfgCommand() == cfgCmdStanzaDelete &&
        !cfgOptionTest(cfgOptRepo) && (cfgOptionGroupIdxTotal(cfgOptGrpRepo) > 1 || cfgOptionGroupIdxToKey(cfgOptGrpRepo, 0) != 1))
    {
        THROW_FMT(
            OptionRequiredError,
            "%s command requires option: " CFGOPT_REPO "\n"
            "HINT: this command requires a specific repository to operate on",
            cfgCommandName());
    }

    // If there is more than one repo configured
    if (cfgOptionGroupIdxTotal(cfgOptGrpRepo) > 1)
    {
        for (unsigned int optionIdx = 0; optionIdx < cfgOptionGroupIdxTotal(cfgOptGrpRepo); optionIdx++)
        {
            // If the repo is local and either posix or cifs
            if (!cfgOptionIdxTest(cfgOptRepoHost, optionIdx) &&
                (cfgOptionIdxStrId(cfgOptRepoType, optionIdx) == STORAGE_POSIX_TYPE ||
                 cfgOptionIdxStrId(cfgOptRepoType, optionIdx) == STORAGE_CIFS_TYPE))
            {
                // Ensure a local repo does not have the same path as another local repo of the same type
                for (unsigned int repoIdx = 0; repoIdx < cfgOptionGroupIdxTotal(cfgOptGrpRepo); repoIdx++)
                {
                    if (optionIdx != repoIdx && !(cfgOptionIdxTest(cfgOptRepoHost, repoIdx)) &&
                        cfgOptionIdxStrId(cfgOptRepoType, optionIdx) == cfgOptionIdxStrId(cfgOptRepoType, repoIdx) &&
                        strEq(cfgOptionIdxStr(cfgOptRepoPath, optionIdx), cfgOptionIdxStr(cfgOptRepoPath, repoIdx)))
                    {
                        THROW_FMT(
                            OptionInvalidValueError,
                            "local %s and %s paths are both '%s' but must be different",
                            cfgOptionGroupName(cfgOptGrpRepo, optionIdx), cfgOptionGroupName(cfgOptGrpRepo, repoIdx),
                            strZ(cfgOptionIdxDisplay(cfgOptRepoPath, repoIdx)));
                    }
                }
            }
        }
    }

    // Protocol timeout should be greater than db timeout
    if (cfgOptionTest(cfgOptDbTimeout) && cfgOptionTest(cfgOptProtocolTimeout) &&
        cfgOptionInt64(cfgOptProtocolTimeout) <= cfgOptionInt64(cfgOptDbTimeout))
    {
        // If protocol-timeout is default then increase it to be greater than db-timeout
        if (cfgOptionSource(cfgOptProtocolTimeout) == cfgSourceDefault)
        {
            cfgOptionSet(
                cfgOptProtocolTimeout, cfgSourceDefault, VARINT64(cfgOptionInt64(cfgOptDbTimeout) + (int64_t)(30 * MSEC_PER_SEC)));
        }
        else if (cfgOptionSource(cfgOptDbTimeout) == cfgSourceDefault)
        {
            const int64_t dbTimeout = cfgOptionInt64(cfgOptProtocolTimeout) - (int64_t)(30 * MSEC_PER_SEC);

            // Normally the protocol time will be greater than 45 seconds so db timeout can be at least 15 seconds
            if (dbTimeout >= (int64_t)(15 * MSEC_PER_SEC))
            {
                cfgOptionSet(cfgOptDbTimeout, cfgSourceDefault, VARINT64(dbTimeout));
            }
            // But in some test cases the protocol timeout will be very small so make db timeout half of protocol timeout
            else
                cfgOptionSet(cfgOptDbTimeout, cfgSourceDefault, VARINT64(cfgOptionInt64(cfgOptProtocolTimeout) / 2));
        }
        else
        {
            THROW_FMT(
                OptionInvalidValueError,
                "'%s' is not valid for '" CFGOPT_PROTOCOL_TIMEOUT "' option\nHINT '" CFGOPT_PROTOCOL_TIMEOUT "' option (%s)"
                " should be greater than '" CFGOPT_DB_TIMEOUT "' option (%s).",
                strZ(cfgOptionDisplay(cfgOptProtocolTimeout)), strZ(cfgOptionDisplay(cfgOptProtocolTimeout)),
                strZ(cfgOptionDisplay(cfgOptDbTimeout)));
        }
    }

    // Make sure that repo and pg host settings are not both set - cannot both be remote
    if (cfgOptionValid(cfgOptPgHost) && cfgOptionValid(cfgOptRepoHost))
    {
        bool pgHostFound = false;

        for (unsigned int optionIdx = 0; optionIdx < cfgOptionGroupIdxTotal(cfgOptGrpPg); optionIdx++)
        {
            if (cfgOptionIdxTest(cfgOptPgHost, optionIdx))
            {
                pgHostFound = true;
                break;
            }
        }

        // If a pg-host was found, see if a repo-host is configured
        if (pgHostFound == true)
        {
            for (unsigned int optionIdx = 0; optionIdx < cfgOptionGroupIdxTotal(cfgOptGrpRepo); optionIdx++)
            {
                if (cfgOptionIdxTest(cfgOptRepoHost, optionIdx))
                    THROW_FMT(ConfigError, "pg and repo hosts cannot both be configured as remote");
            }
        }
    }

    // Warn when repo-retention-full is not set on a configured repo
    if (!cfgCommandHelp() && cfgOptionValid(cfgOptRepoRetentionFullType))
    {
        for (unsigned int optionIdx = 0; optionIdx < cfgOptionGroupIdxTotal(cfgOptGrpRepo); optionIdx++)
        {
            if (!(cfgOptionIdxTest(cfgOptRepoRetentionFull, optionIdx)))
            {
                LOG_WARN_FMT(
                    "option '%s' is not set for '%s=%s', the repository may run out of space"
                    "\nHINT: to retain full backups indefinitely (without warning), set option '%s' to the maximum.",
                    cfgOptionIdxName(cfgOptRepoRetentionFull, optionIdx), cfgOptionIdxName(cfgOptRepoRetentionFullType, optionIdx),
                    strZ(cfgOptionIdxDisplay(cfgOptRepoRetentionFullType, optionIdx)),
                    cfgOptionIdxName(cfgOptRepoRetentionFull, optionIdx));
            }
        }
    }

    // If archive retention is valid for the command, then set archive settings
    if (cfgOptionValid(cfgOptRepoRetentionArchive))
    {
        // For each possible repo, check and adjust the settings as appropriate
        for (unsigned int optionIdx = 0; optionIdx < cfgOptionGroupIdxTotal(cfgOptGrpRepo); optionIdx++)
        {
            const BackupType archiveRetentionType = (BackupType)cfgOptionIdxStrId(cfgOptRepoRetentionArchiveType, optionIdx);

            // If the archive retention is not explicitly set then determine what it should be defaulted to
            if (!cfgOptionIdxTest(cfgOptRepoRetentionArchive, optionIdx))
            {
                // If repo-retention-archive-type is default (full), then if repo-retention-full is set, set the
                // repo-retention-archive to this value when retention-full-type is 'count', else ignore archiving. If
                // retention-full-type is 'time' then the expire command will default the archive retention accordingly.
                const String *const msgArchiveOff = strNewFmt(
                    "WAL segments will not be expired: option '%s=%s' but",
                    cfgOptionIdxName(cfgOptRepoRetentionArchiveType, optionIdx), strZ(strIdToStr(archiveRetentionType)));

                switch (archiveRetentionType)
                {
                    case backupTypeFull:
                    {
                        if (cfgOptionIdxStrId(cfgOptRepoRetentionFullType, optionIdx) == CFGOPTVAL_REPO_RETENTION_FULL_TYPE_COUNT &&
                            cfgOptionIdxTest(cfgOptRepoRetentionFull, optionIdx))
                        {
                            cfgOptionIdxSet(
                                cfgOptRepoRetentionArchive, optionIdx, cfgSourceDefault,
                                VARINT64(cfgOptionIdxInt64(cfgOptRepoRetentionFull, optionIdx)));
                        }

                        break;
                    }

                    case backupTypeDiff:
                    {
                        // if repo-retention-diff is set then user must have set it
                        if (cfgOptionIdxTest(cfgOptRepoRetentionDiff, optionIdx))
                        {
                            cfgOptionIdxSet(
                                cfgOptRepoRetentionArchive, optionIdx, cfgSourceDefault,
                                VARINT64(cfgOptionIdxInt64(cfgOptRepoRetentionDiff, optionIdx)));
                        }
                        else
                        {
                            LOG_WARN_FMT(
                                "%s neither option '%s' nor option '%s' is set", strZ(msgArchiveOff),
                                cfgOptionIdxName(cfgOptRepoRetentionArchive, optionIdx),
                                cfgOptionIdxName(cfgOptRepoRetentionDiff, optionIdx));
                        }

                        break;
                    }

                    case backupTypeIncr:
                        LOG_WARN_FMT(
                            "%s option '%s' is not set", strZ(msgArchiveOff),
                            cfgOptionIdxName(cfgOptRepoRetentionArchive, optionIdx));
                        break;
                }
            }
            else
            {
                // If repo-retention-archive is set then check repo-retention-archive-type and issue a warning if the
                // corresponding setting is UNDEF since UNDEF means backups will not be expired but they should be in the
                // practice of setting this value even though expiring the archive itself is OK and will be performed.
                if (archiveRetentionType == backupTypeDiff && !cfgOptionIdxTest(cfgOptRepoRetentionDiff, optionIdx))
                {
                    LOG_WARN_FMT(
                        "option '%s' is not set for '%s=" CFGOPTVAL_TYPE_DIFF_Z "'\n"
                        "HINT: to retain differential backups indefinitely (without warning), set option '%s' to the maximum.",
                        cfgOptionIdxName(cfgOptRepoRetentionDiff, optionIdx),
                        cfgOptionIdxName(cfgOptRepoRetentionArchiveType, optionIdx),
                        cfgOptionIdxName(cfgOptRepoRetentionDiff, optionIdx));
                }
            }
        }
    }

    // For each possible repo, error if an S3 bucket name contains dots
    for (unsigned int repoIdx = 0; repoIdx < cfgOptionGroupIdxTotal(cfgOptGrpRepo); repoIdx++)
    {
        if (cfgOptionIdxTest(cfgOptRepoS3Bucket, repoIdx) && cfgOptionIdxBool(cfgOptRepoStorageVerifyTls, repoIdx) &&
            strChr(cfgOptionIdxStr(cfgOptRepoS3Bucket, repoIdx), '.') != -1)
        {
            THROW_FMT(
                OptionInvalidValueError,
                "'%s' is not valid for option '%s'\n"
                "HINT: RFC-2818 forbids dots in wildcard matches.\n"
                "HINT: TLS/SSL verification cannot proceed with this bucket name.\n"
                "HINT: remove dots from the bucket name.",
                strZ(cfgOptionIdxDisplay(cfgOptRepoS3Bucket, repoIdx)), cfgOptionIdxName(cfgOptRepoS3Bucket, repoIdx));
        }
    }

    // Check/update compress-type if compress is valid. There should be no references to the compress option outside this block.
    if (cfgOptionValid(cfgOptCompress))
    {
        if (cfgOptionSource(cfgOptCompress) != cfgSourceDefault)
        {
            if (cfgOptionSource(cfgOptCompressType) != cfgSourceDefault)
            {
                LOG_WARN(
                    "'" CFGOPT_COMPRESS "' and '" CFGOPT_COMPRESS_TYPE "' options should not both be set\n"
                    "HINT: '" CFGOPT_COMPRESS_TYPE "' is preferred and '" CFGOPT_COMPRESS "' is deprecated.");
            }

            // Set compress-type to none. Eventually the compress option will be deprecated and removed so this reduces code churn
            // when that happens.
            if (!cfgOptionBool(cfgOptCompress) && cfgOptionSource(cfgOptCompressType) == cfgSourceDefault)
            {
                cfgOptionSet(cfgOptCompressType, cfgSourceParam, VARUINT64(CFGOPTVAL_COMPRESS_TYPE_NONE));
                cfgOptionSet(cfgOptCompressLevel, cfgSourceDefault, VARINT64(0));
            }
        }

        // Now invalidate compress so it can't be used and won't be passed to child processes
        cfgOptionInvalidate(cfgOptCompress);
        cfgOptionSet(cfgOptCompress, cfgSourceDefault, NULL);
    }

    // Error if repo-sftp--host-key-check-type is explicitly set to anything other than fingerprint and repo-sftp-host-fingerprint
    // is also specified. For backward compatibility we need to allow repo-sftp-host-fingerprint when
    // repo-sftp-host-key-check-type defaults to yes, but emit a warning to let the user know to change the configuration. Also
    // set repo-sftp-host-key-check-type=fingerprint so other code does not need to know about this exception.
    for (unsigned int repoIdx = 0; repoIdx < cfgOptionGroupIdxTotal(cfgOptGrpRepo); repoIdx++)
    {
        if (cfgOptionIdxTest(cfgOptRepoSftpHostKeyCheckType, repoIdx))
        {
            if (cfgOptionIdxTest(cfgOptRepoSftpHostFingerprint, repoIdx))
            {
                if (cfgOptionIdxSource(cfgOptRepoSftpHostKeyCheckType, repoIdx) == cfgSourceDefault)
                {
                    LOG_WARN_FMT(
                        "option '%s' without option '%s' = '" CFGOPTVAL_REPO_SFTP_HOST_KEY_CHECK_TYPE_FINGERPRINT_Z "' is"
                        " deprecated\n"
                        "HINT: set option '%s=" CFGOPTVAL_REPO_SFTP_HOST_KEY_CHECK_TYPE_FINGERPRINT_Z "'",
                        cfgOptionIdxName(cfgOptRepoSftpHostFingerprint, repoIdx),
                        cfgOptionIdxName(cfgOptRepoSftpHostKeyCheckType, repoIdx),
                        cfgOptionIdxName(cfgOptRepoSftpHostKeyCheckType, repoIdx));

                    cfgOptionIdxSet(
                        cfgOptRepoSftpHostKeyCheckType, repoIdx, cfgSourceDefault,
                        VARSTRZ(CFGOPTVAL_REPO_SFTP_HOST_KEY_CHECK_TYPE_FINGERPRINT_Z));
                }
                else
                {
                    if (cfgOptionIdxStrId(cfgOptRepoSftpHostKeyCheckType, repoIdx) !=
                        CFGOPTVAL_REPO_SFTP_HOST_KEY_CHECK_TYPE_FINGERPRINT)
                    {
                        THROW_FMT(
                            OptionInvalidError,
                            "option '%s' not valid without option '%s' = '" CFGOPTVAL_REPO_SFTP_HOST_KEY_CHECK_TYPE_FINGERPRINT_Z
                            "'",
                            cfgOptionIdxName(cfgOptRepoSftpHostFingerprint, repoIdx),
                            cfgOptionIdxName(cfgOptRepoSftpHostKeyCheckType, repoIdx));
                    }
                }
            }
            else
            {
                if (cfgOptionIdxStrId(cfgOptRepoSftpHostKeyCheckType, repoIdx) ==
                    CFGOPTVAL_REPO_SFTP_HOST_KEY_CHECK_TYPE_FINGERPRINT)
                {
                    THROW_FMT(
                        OptionRequiredError, "%s command requires option: %s", cfgCommandName(),
                        cfgOptionIdxName(cfgOptRepoSftpHostFingerprint, repoIdx));
                }
            }
        }
    }

    // A repo must be specified when targeting time. Not all repo types support versioning so rather than try to skip repos in that
    // case it seems to be easier to just target a specific repo. Also, depending on the type of corruption, different repos might
    // require different target times.
    if (cfgOptionTest(cfgOptRepoTargetTime) && cfgOptionSource(cfgOptRepo) == cfgSourceDefault)
        THROW_FMT(OptionInvalidError, "option '" CFGOPT_REPO_TARGET_TIME "' not valid without option '" CFGOPT_REPO "'");

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN String *
cfgLoadLogFileName(const ConfigCommandRole commandRole)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(ENUM, commandRole);
    FUNCTION_LOG_END();

    // Construct log filename prefix
    String *const result = strCatFmt(
        strNew(),
        "%s/%s-%s", strZ(cfgOptionStr(cfgOptLogPath)),
        cfgOptionTest(cfgOptStanza) ? strZ(cfgOptionStr(cfgOptStanza)) : "all", cfgCommandName());

    // ??? Append async for local/remote archive async commands. It would be good to find a more generic way to do this in case the
    // async role is added to more commands.
    if (commandRole == cfgCmdRoleLocal || commandRole == cfgCmdRoleRemote)
    {
        if (cfgOptionValid(cfgOptArchiveAsync) && cfgOptionBool(cfgOptArchiveAsync))
            strCatZ(result, "-async");
    }

    // Add command role if it is not main
    if (commandRole != cfgCmdRoleMain)
        strCatFmt(result, "-%s", strZ(cfgParseCommandRoleStr(commandRole)));

    // Add process id if local or remote role
    if (commandRole == cfgCmdRoleLocal || commandRole == cfgCmdRoleRemote)
        strCatFmt(result, "-%03u", cfgOptionUInt(cfgOptProcess));

    // Add extension
    strCatZ(result, ".log");

    FUNCTION_LOG_RETURN(STRING, result);
}

/**********************************************************************************************************************************/
FN_EXTERN void
cfgLoadLogFile(void)
{
    if (cfgLogFile() && !cfgCommandHelp())
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            // Attempt to open log file
            if (!logFileSet(strZ(cfgLoadLogFileName(cfgCommandRole()))))
                cfgOptionSet(cfgOptLogLevelFile, cfgSourceParam, VARUINT64(CFGOPTVAL_LOG_LEVEL_CONSOLE_OFF));
        }
        MEM_CONTEXT_TEMP_END();
    }
}

/**********************************************************************************************************************************/
FN_EXTERN void
cfgLoad(const unsigned int argListSize, const char *argList[])
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, argListSize);
        FUNCTION_LOG_PARAM(CHARPY, argList);
    FUNCTION_LOG_END();

    ASSERT(argListSize > 0);
    ASSERT(argList != NULL);

    // Store arguments
    configLoadLocal.argListSize = argListSize;
    configLoadLocal.argList = argList;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Parse config from command line and config file
        cfgParseP(storageLocal(), configLoadLocal.argListSize, configLoadLocal.argList);

        // Initialize dry-run mode for storage when valid for the current command
        storageHelperDryRunInit(cfgOptionValid(cfgOptDryRun) && cfgOptionBool(cfgOptDryRun));

        // Load the log settings
        cfgLoadLogSetting();

        // Neutralize the umask to make the repository file/path modes more consistent
        if (cfgOptionValid(cfgOptNeutralUmask) && cfgOptionBool(cfgOptNeutralUmask))
            umask(0000);

        // Initialize TCP settings
        if (cfgOptionValid(cfgOptSckKeepAlive))
        {
            sckInit(
                cfgOptionBool(cfgOptSckBlock),
                cfgOptionBool(cfgOptSckKeepAlive),
                cfgOptionTest(cfgOptTcpKeepAliveCount) ? cfgOptionInt(cfgOptTcpKeepAliveCount) : 0,
                cfgOptionTest(cfgOptTcpKeepAliveIdle) ? cfgOptionInt(cfgOptTcpKeepAliveIdle) : 0,
                cfgOptionTest(cfgOptTcpKeepAliveInterval) ? cfgOptionInt(cfgOptTcpKeepAliveInterval) : 0);
        }

        // Set IO buffer size (use the default for help to lower memory usage)
        if (cfgOptionValid(cfgOptBufferSize) && !cfgCommandHelp())
            ioBufferSizeSet(cfgOptionUInt(cfgOptBufferSize));

        // Set IO timeout
        if (cfgOptionValid(cfgOptIoTimeout))
            ioTimeoutMsSet(cfgOptionUInt64(cfgOptIoTimeout));

        // Open the log file if this command logs to a file
        cfgLoadLogFile();

        // Create the exec-id used to identify all locals and remotes spawned by this process. This allows lock contention to be
        // easily resolved and makes it easier to associate processes from various logs.
        if (cfgOptionValid(cfgOptExecId) && !cfgOptionTest(cfgOptExecId))
        {
            // Generate some random bytes
            uint32_t execRandom;
            cryptoRandomBytes((uint8_t *)&execRandom, sizeof(execRandom));

            // Format a string with the pid and the random bytes to serve as the exec id
            cfgOptionSet(cfgOptExecId, cfgSourceParam, VARSTR(strNewFmt("%d-%08x", getpid(), execRandom)));
        }

        // Begin the command
        cmdBegin();

        // Initialize the lock module
        if (cfgOptionTest(cfgOptLockPath))
            lockInit(cfgOptionStr(cfgOptLockPath), cfgOptionStr(cfgOptExecId));

        // Acquire a lock if this command requires a lock
        if (cfgLockType() != lockTypeNone && !cfgCommandHelp() && cfgLockRequired())
            cmdLockAcquireP();

        // Update options that have complex rules
        cfgLoadUpdateOption();
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN void
cfgLoadStanza(const String *const stanza)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING, stanza);
    FUNCTION_LOG_END();

    ASSERT(stanza != NULL);
    ASSERT(configLoadLocal.argListSize > 0);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Store the exec id so it can be preserved after reload
        const Variant *const execId = varNewStr(cfgOptionStr(cfgOptExecId));

        // Make a copy of the arguments and add the stanza (this assumes the stanza was not originally specified)
        StringList *const argListNew = strLstNew();

        for (unsigned int argListIdx = 0; argListIdx < configLoadLocal.argListSize; argListIdx++)
            strLstAddZ(argListNew, configLoadLocal.argList[argListIdx]);

        strLstAddFmt(argListNew, "--" CFGOPT_STANZA "=%s", strZ(stanza));

        // Parse config from command line and config file
        cfgParseP(storageLocal(), strLstSize(argListNew), strLstPtr(argListNew), .noConfigLoad = true, .noResetLogLevel = true);

        // Update options that have complex rules
        cfgLoadUpdateOption();

        // Set execId to prior value
        cfgOptionSet(cfgOptExecId, cfgSourceParam, execId);
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
