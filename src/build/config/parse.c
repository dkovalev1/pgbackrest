/***********************************************************************************************************************************
Parse Configuration Yaml
***********************************************************************************************************************************/
#include "build.auto.h"

#include <yaml.h>

#include "common/log.h"
#include "common/type/convert.h"
#include "storage/posix/storage.h"

#include "build/common/yaml.h"
#include "build/config/parse.h"

/***********************************************************************************************************************************
Command role constants
***********************************************************************************************************************************/
STRING_EXTERN(CMD_ROLE_ASYNC_STR,                                   CMD_ROLE_ASYNC);
STRING_EXTERN(CMD_ROLE_LOCAL_STR,                                   CMD_ROLE_LOCAL);
STRING_EXTERN(CMD_ROLE_MAIN_STR,                                    CMD_ROLE_MAIN);
STRING_EXTERN(CMD_ROLE_REMOTE_STR,                                  CMD_ROLE_REMOTE);

/***********************************************************************************************************************************
Command constants
***********************************************************************************************************************************/
STRING_EXTERN(CMD_HELP_STR,                                         CMD_HELP);
STRING_EXTERN(CMD_VERSION_STR,                                      CMD_VERSION);

/***********************************************************************************************************************************
Option type constants
***********************************************************************************************************************************/
STRING_EXTERN(OPT_TYPE_BOOLEAN_STR,                                 OPT_TYPE_BOOLEAN);
STRING_EXTERN(OPT_TYPE_HASH_STR,                                    OPT_TYPE_HASH);
STRING_EXTERN(OPT_TYPE_INTEGER_STR,                                 OPT_TYPE_INTEGER);
STRING_EXTERN(OPT_TYPE_LIST_STR,                                    OPT_TYPE_LIST);
STRING_EXTERN(OPT_TYPE_PATH_STR,                                    OPT_TYPE_PATH);
STRING_EXTERN(OPT_TYPE_SIZE_STR,                                    OPT_TYPE_SIZE);
STRING_EXTERN(OPT_TYPE_STRING_STR,                                  OPT_TYPE_STRING);
STRING_EXTERN(OPT_TYPE_STRING_ID_STR,                               OPT_TYPE_STRING_ID);
STRING_EXTERN(OPT_TYPE_TIME_STR,                                    OPT_TYPE_TIME);

/***********************************************************************************************************************************
Option constants
***********************************************************************************************************************************/
STRING_EXTERN(OPT_BETA_STR,                                         OPT_BETA);
STRING_EXTERN(OPT_STANZA_STR,                                       OPT_STANZA);

/***********************************************************************************************************************************
Section constants
***********************************************************************************************************************************/
STRING_EXTERN(SECTION_COMMAND_LINE_STR,                             SECTION_COMMAND_LINE);
STRING_EXTERN(SECTION_GLOBAL_STR,                                   SECTION_GLOBAL);
STRING_EXTERN(SECTION_STANZA_STR,                                   SECTION_STANZA);

/***********************************************************************************************************************************
Parse command list
***********************************************************************************************************************************/
typedef struct BldCfgCommandRaw
{
    const String *const name;                                       // See BldCfgCommand for comments
    bool internal;
    bool logFile;
    const String *logLevelDefault;
    bool lockRequired;
    bool lockRemoteRequired;
    const String *lockType;
    bool parameterAllowed;
    StringList *roleList;
} BldCfgCommandRaw;

// Helper to parse command roles
static StringList *
bldCfgParseCommandRole(Yaml *const yaml)
{
    StringList *const result = strLstNew();

    MEM_CONTEXT_TEMP_BEGIN()
    {
        yamlEventNextCheck(yaml, yamlEventTypeMapBegin);
        YamlEvent commandRoleVal = yamlEventNext(yaml);

        if (commandRoleVal.type != yamlEventTypeMapEnd)
        {
            do
            {
                yamlEventCheck(commandRoleVal, yamlEventTypeScalar);

                strLstAdd(result, commandRoleVal.value);

                yamlEventNextCheck(yaml, yamlEventTypeMapBegin);
                yamlEventNextCheck(yaml, yamlEventTypeMapEnd);

                commandRoleVal = yamlEventNext(yaml);
            }
            while (commandRoleVal.type != yamlEventTypeMapEnd);
        }
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

static List *
bldCfgParseCommandList(Yaml *const yaml)
{
    List *const result = lstNewP(sizeof(BldCfgCommand), .comparator = lstComparatorStr);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        yamlEventNextCheck(yaml, yamlEventTypeScalar);
        yamlEventNextCheck(yaml, yamlEventTypeMapBegin);

        YamlEvent cmd = yamlEventNext(yaml);

        do
        {
            yamlEventCheck(cmd, yamlEventTypeScalar);

            BldCfgCommandRaw cmdRaw =
            {
                .name = cmd.value,
                .logFile = true,
                .logLevelDefault = strNewZ("info"),
                .lockType = strNewZ("none"),
            };

            yamlEventNextCheck(yaml, yamlEventTypeMapBegin);

            YamlEvent cmdDef = yamlEventNext(yaml);

            if (cmdDef.type == yamlEventTypeScalar)
            {
                do
                {
                    yamlEventCheck(cmdDef, yamlEventTypeScalar);

                    if (strEqZ(cmdDef.value, "command-role"))
                    {
                        cmdRaw.roleList = bldCfgParseCommandRole(yaml);
                    }
                    else
                    {
                        YamlEvent cmdDefVal = yamlEventNextCheck(yaml, yamlEventTypeScalar);

                        if (strEqZ(cmdDef.value, "internal"))
                        {
                            cmdRaw.internal = yamlBoolParse(cmdDefVal);
                        }
                        else if (strEqZ(cmdDef.value, "lock-type"))
                        {
                            cmdRaw.lockType = cmdDefVal.value;
                        }
                        else if (strEqZ(cmdDef.value, "lock-remote-required"))
                        {
                            cmdRaw.lockRemoteRequired = yamlBoolParse(cmdDefVal);
                        }
                        else if (strEqZ(cmdDef.value, "lock-required"))
                        {
                            cmdRaw.lockRequired = yamlBoolParse(cmdDefVal);
                        }
                        else if (strEqZ(cmdDef.value, "log-file"))
                        {
                            cmdRaw.logFile = yamlBoolParse(cmdDefVal);
                        }
                        else if (strEqZ(cmdDef.value, "log-level-default"))
                        {
                            cmdRaw.logLevelDefault = strLower(strDup(cmdDefVal.value));
                        }
                        else if (strEqZ(cmdDef.value, "parameter-allowed"))
                        {
                            cmdRaw.parameterAllowed = strLower(strDup(cmdDefVal.value));
                        }
                        else
                            THROW_FMT(FormatError, "unknown command definition '%s'", strZ(cmdDef.value));
                    }

                    cmdDef = yamlEventNext(yaml);
                }
                while (cmdDef.type != yamlEventTypeMapEnd);
            }
            else
                yamlEventCheck(cmdDef, yamlEventTypeMapEnd);

            // Create role list if not defined
            if (cmdRaw.roleList == NULL)
                cmdRaw.roleList = strLstNew();

            // Add main to the role list and resort
            strLstAddIfMissing(cmdRaw.roleList, CMD_ROLE_MAIN_STR);
            strLstSort(cmdRaw.roleList, sortOrderAsc);

            MEM_CONTEXT_BEGIN(lstMemContext(result))
            {
                const BldCfgCommand bldCfgCommand =
                {
                    .name = strDup(cmdRaw.name),
                    .internal = cmdRaw.internal,
                    .logFile = cmdRaw.logFile,
                    .logLevelDefault = strDup(cmdRaw.logLevelDefault),
                    .lockRequired = cmdRaw.lockRequired,
                    .lockRemoteRequired = cmdRaw.lockRemoteRequired,
                    .lockType = strDup(cmdRaw.lockType),
                    .parameterAllowed = cmdRaw.parameterAllowed,
                    .roleList = strLstDup(cmdRaw.roleList)
                };

                lstAdd(result, &bldCfgCommand);
            }
            MEM_CONTEXT_END();

            cmd = yamlEventNext(yaml);
        }
        while (cmd.type != yamlEventTypeMapEnd);

        lstSort(result, sortOrderAsc);
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

/***********************************************************************************************************************************
Parse option group list
***********************************************************************************************************************************/
typedef struct BldCfgOptionGroupRaw
{
    const String *const name;                                       // See BldCfgOptionGroup for comments
} BldCfgOptionGroupRaw;

static List *
bldCfgParseOptionGroupList(Yaml *const yaml)
{
    List *const result = lstNewP(sizeof(BldCfgOptionGroup), .comparator = lstComparatorStr);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        yamlEventNextCheck(yaml, yamlEventTypeScalar);
        yamlEventNextCheck(yaml, yamlEventTypeMapBegin);

        YamlEvent optGrp = yamlEventNext(yaml);

        do
        {
            yamlEventCheck(optGrp, yamlEventTypeScalar);
            BldCfgOptionGroupRaw optGrpRaw = {.name = optGrp.value};

            yamlEventNextCheck(yaml, yamlEventTypeMapBegin);
            yamlEventNextCheck(yaml, yamlEventTypeMapEnd);

            MEM_CONTEXT_BEGIN(lstMemContext(result))
            {
                lstAdd(result, &(BldCfgOptionGroup){.name = strDup(optGrpRaw.name)});
            }
            MEM_CONTEXT_END();

            optGrp = yamlEventNext(yaml);
        }
        while (optGrp.type != yamlEventTypeMapEnd);

        lstSort(result, sortOrderAsc);
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

/***********************************************************************************************************************************
Parse option list
***********************************************************************************************************************************/
typedef struct BldCfgOptionAllowRangeMapRaw                         // See BldCfgOptionAllowRangeMap for comments
{
    const String *map;
    const String *min;
    const String *max;
} BldCfgOptionAllowRangeMapRaw;

typedef struct BldCfgOptionAllowRangeRaw                            // See BldCfgOptionAllowRange for comments
{
    const String *min;
    const String *max;
    List *mapList;
} BldCfgOptionAllowRangeRaw;

typedef struct BldCfgOptionDefaultMapRaw
{
    const String *map;                                              // See BldCfgOptionDefaultMap for comments
    const String *value;
} BldCfgOptionDefaultMapRaw;

typedef struct BldCfgOptionDefaultRaw
{
    const String *value;                                            // See BldCfgOptionDefault for comments
    List *mapList;
} BldCfgOptionDefaultRaw;

typedef struct BldCfgOptionDependRaw
{
    const String *option;                                           // See BldCfgOptionDepend for comments
    const String *defaultValue;
    const StringList *valueList;
} BldCfgOptionDependRaw;

typedef struct BldCfgOptionDeprecateRaw
{
    const String *name;                                             // See BldCfgOptionDeprecate for comments
    bool indexed;
    bool unindexed;
} BldCfgOptionDeprecateRaw;

typedef struct BldCfgOptionCommandRaw
{
    const String *name;                                             // See BldCfgOptionCommand for comments
    const Variant *internal;
    const Variant *required;
    const BldCfgOptionDefaultRaw *defaultValue;
    const BldCfgOptionDependRaw *depend;
    const List *allowList;
    const StringList *roleList;
} BldCfgOptionCommandRaw;

typedef struct BldCfgOptionRaw
{
    const String *name;                                             // See BldCfgOption for comments
    const String *type;
    const String *section;
    bool boolLike;
    bool internal;
    bool beta;
    const Variant *required;
    const Variant *negate;
    bool reset;
    DefaultType defaultType;
    const BldCfgOptionDefaultRaw *defaultValue;
    const String *group;
    bool secure;
    const BldCfgOptionDependRaw *depend;
    const List *allowList;
    const BldCfgOptionAllowRangeRaw *allowRange;
    const List *cmdList;
    const StringList *cmdRoleList;
    const List *deprecateList;
} BldCfgOptionRaw;

// Helper to parse allow list
static List *
bldCfgParseAllowListDup(const List *const allowList)
{
    List *result = NULL;

    if (allowList != NULL)
    {
        result = lstNewP(sizeof(BldCfgOptionValue));

        for (unsigned int valueIdx = 0; valueIdx < lstSize(allowList); valueIdx++)
        {
            const BldCfgOptionValue *const bldCfgOptionValue = lstGet(allowList, valueIdx);
            const BldCfgOptionValue bldCfgOptionValueCopy =
            {
                .value = strDup(bldCfgOptionValue->value),
                .condition = strDup(bldCfgOptionValue->condition),
            };

            lstAdd(result, &bldCfgOptionValueCopy);
        }
    }

    return result;
}

static const List *
bldCfgParseAllowList(Yaml *const yaml, const List *const optList)
{
    List *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        YamlEvent allowListVal = yamlEventNext(yaml);

        // If allow list is defined
        if (allowListVal.type == yamlEventTypeSeqBegin)
        {
            YamlEvent allowListVal = yamlEventNext(yaml);

            MEM_CONTEXT_PRIOR_BEGIN()
            {
                result = lstNewP(sizeof(BldCfgOptionValue));
            }
            MEM_CONTEXT_PRIOR_END();

            do
            {
                BldCfgOptionValue bldCfgOptionValue;

                if (allowListVal.type == yamlEventTypeScalar)
                {
                    bldCfgOptionValue.value = allowListVal.value;
                    bldCfgOptionValue.condition = NULL;
                }
                else
                {
                    yamlEventCheck(allowListVal, yamlEventTypeMapBegin);

                    bldCfgOptionValue.value = yamlEventNextCheck(yaml, yamlEventTypeScalar).value;
                    bldCfgOptionValue.condition = yamlEventNextCheck(yaml, yamlEventTypeScalar).value;

                    yamlEventNextCheck(yaml, yamlEventTypeMapEnd);
                }

                lstAdd(result, &bldCfgOptionValue);

                allowListVal = yamlEventNext(yaml);
            }
            while (allowListVal.type != yamlEventTypeSeqEnd);
        }
        else
        {
            // Else allow list is inherited
            CHECK(AssertError, optList != NULL, "option list is NULL");
            yamlEventCheck(allowListVal, yamlEventTypeScalar);

            const BldCfgOptionRaw *const optInherit = lstFind(optList, &allowListVal.value);
            CHECK(AssertError, optInherit != NULL, "inherited option is NULL");

            MEM_CONTEXT_PRIOR_BEGIN()
            {
                result = bldCfgParseAllowListDup(optInherit->allowList);
            }
            MEM_CONTEXT_PRIOR_END();
        }

        CHECK(AssertError, result != NULL, "result is NULL");
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

// Helper to parse allow range
static const BldCfgOptionAllowRange *
bldCfgParseAllowRangeDup(const BldCfgOptionAllowRangeRaw *const allowRangeRaw)
{
    BldCfgOptionAllowRange *result = NULL;

    if (allowRangeRaw != NULL)
    {
        result = memNew(sizeof(BldCfgOptionAllowRange));
        *result = (BldCfgOptionAllowRange){0};

        result->min = strDup(allowRangeRaw->min);
        result->max = strDup(allowRangeRaw->max);

        if (allowRangeRaw->mapList != NULL)
        {
            List *const mapList = lstNewP(sizeof(BldCfgOptionAllowRangeMap), .comparator = lstComparatorStr);

            for (unsigned int mapIdx = 0; mapIdx < lstSize(allowRangeRaw->mapList); mapIdx++)
            {
                const BldCfgOptionAllowRangeMapRaw *const allowRangeMapRaw = lstGet(allowRangeRaw->mapList, mapIdx);
                const BldCfgOptionAllowRangeMap allowRangeMap =
                {
                    .map = strDup(allowRangeMapRaw->map),
                    .min = strDup(allowRangeMapRaw->min),
                    .max = strDup(allowRangeMapRaw->max),
                };

                lstAdd(mapList, &allowRangeMap);
            }

            result->mapList = mapList;
        }
    }

    return result;
}

static const BldCfgOptionAllowRangeRaw *
bldCfgParseAllowRange(Yaml *const yaml)
{
    BldCfgOptionAllowRangeRaw *result = memNew(sizeof(BldCfgOptionAllowRangeRaw));
    *result = (BldCfgOptionAllowRangeRaw){0};

    MEM_CONTEXT_TEMP_BEGIN()
    {
        yamlEventNextCheck(yaml, yamlEventTypeSeqBegin);

        if (yamlEventPeek(yaml).type == yamlEventTypeScalar)
        {
            YamlEvent allowRangeMinVal = yamlEventNextCheck(yaml, yamlEventTypeScalar);
            YamlEvent allowRangeMaxVal = yamlEventNextCheck(yaml, yamlEventTypeScalar);

            MEM_CONTEXT_PRIOR_BEGIN()
            {
                result->min = strDup(allowRangeMinVal.value);
                result->max = strDup(allowRangeMaxVal.value);
            }
            MEM_CONTEXT_PRIOR_END();

            yamlEventNextCheck(yaml, yamlEventTypeSeqEnd);
        }
        else
        {
            YamlEvent allowRangeVal = yamlEventNext(yaml);

            MEM_CONTEXT_PRIOR_BEGIN()
            {
                result->mapList = lstNewP(sizeof(BldCfgOptionAllowRangeMapRaw));
            }
            MEM_CONTEXT_PRIOR_END();

            do
            {
                yamlEventCheck(allowRangeVal, yamlEventTypeMapBegin);

                const String *const map = yamlEventNextCheck(yaml, yamlEventTypeScalar).value;

                yamlEventNextCheck(yaml, yamlEventTypeSeqBegin);

                YamlEvent allowRangeMinVal = yamlEventNextCheck(yaml, yamlEventTypeScalar);
                YamlEvent allowRangeMaxVal = yamlEventNextCheck(yaml, yamlEventTypeScalar);

                MEM_CONTEXT_PRIOR_BEGIN()
                {
                    const BldCfgOptionAllowRangeMapRaw allowRangeMap =
                    {
                        .map = strDup(map),
                        .min = strDup(allowRangeMinVal.value),
                        .max = strDup(allowRangeMaxVal.value),
                    };

                    lstAdd(result->mapList, &allowRangeMap);
                }
                MEM_CONTEXT_PRIOR_END();

                yamlEventNextCheck(yaml, yamlEventTypeSeqEnd);
                yamlEventNextCheck(yaml, yamlEventTypeMapEnd);

                allowRangeVal = yamlEventNext(yaml);
            }
            while (allowRangeVal.type != yamlEventTypeSeqEnd);
        }
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

// Helper to parse default
static const BldCfgOptionDefault *
bldCfgParseDefaultDup(const BldCfgOptionDefaultRaw *const defaultRaw)
{
    BldCfgOptionDefault *result = NULL;

    if (defaultRaw != NULL)
    {
        result = memNew(sizeof(BldCfgOptionDefault));
        *result = (BldCfgOptionDefault){0};
        result->value = strDup(defaultRaw->value);

        if (defaultRaw->mapList != NULL)
        {
            List *const mapList = lstNewP(sizeof(BldCfgOptionDefaultMap), .comparator = lstComparatorStr);

            for (unsigned int mapIdx = 0; mapIdx < lstSize(defaultRaw->mapList); mapIdx++)
            {
                const BldCfgOptionDefaultMapRaw *const defaultMapRaw = lstGet(defaultRaw->mapList, mapIdx);
                const BldCfgOptionDefaultMap defaultMap =
                {
                    .map = strDup(defaultMapRaw->map),
                    .value = strDup(defaultMapRaw->value),
                };

                lstAdd(mapList, &defaultMap);
            }

            result->mapList = mapList;
        }
    }

    return result;
}

static const BldCfgOptionDefaultRaw *
bldCfgParseDefault(Yaml *const yaml)
{
    BldCfgOptionDefaultRaw *result = memNew(sizeof(BldCfgOptionDefaultRaw));
    *result = (BldCfgOptionDefaultRaw){0};

    MEM_CONTEXT_TEMP_BEGIN()
    {
        YamlEvent defaultVal = yamlEventNext(yaml);

        if (defaultVal.type == yamlEventTypeScalar)
        {
            // If an override to inheritance then set to NULL
            if (strEqZ(defaultVal.value, "~"))
                result = NULL;
            else
            {
                MEM_CONTEXT_PRIOR_BEGIN()
                {
                    result->value = strDup(defaultVal.value);
                }
                MEM_CONTEXT_PRIOR_END();
            }
        }
        else
        {
            yamlEventCheck(defaultVal, yamlEventTypeSeqBegin);
            defaultVal = yamlEventNext(yaml);

            MEM_CONTEXT_PRIOR_BEGIN()
            {
                result->mapList = lstNewP(sizeof(BldCfgOptionDefaultMapRaw));
            }
            MEM_CONTEXT_PRIOR_END();

            do
            {
                yamlEventCheck(defaultVal, yamlEventTypeMapBegin);

                const String *const map = yamlEventNextCheck(yaml, yamlEventTypeScalar).value;
                const String *const value = yamlEventNextCheck(yaml, yamlEventTypeScalar).value;

                MEM_CONTEXT_PRIOR_BEGIN()
                {
                    const BldCfgOptionDefaultMapRaw defaultMap =
                    {
                        .map = strDup(map),
                        .value = strDup(value),
                    };

                    lstAdd(result->mapList, &defaultMap);
                }
                MEM_CONTEXT_PRIOR_END();

                yamlEventNextCheck(yaml, yamlEventTypeMapEnd);

                defaultVal = yamlEventNext(yaml);
            }
            while (defaultVal.type != yamlEventTypeSeqEnd);
        }
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

// Helper to parse depend
static const BldCfgOptionDependRaw *
bldCfgParseDepend(Yaml *const yaml, const List *const optList)
{
    const BldCfgOptionDependRaw *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        YamlEvent dependVal = yamlEventNext(yaml);

        if (dependVal.type == yamlEventTypeMapBegin)
        {
            YamlEvent dependDef = yamlEventNext(yaml);
            BldCfgOptionDependRaw optDependRaw = {0};

            do
            {
                yamlEventCheck(dependDef, yamlEventTypeScalar);

                if (strEqZ(dependDef.value, "list"))
                {
                    yamlEventNextCheck(yaml, yamlEventTypeSeqBegin);
                    YamlEvent dependDefVal = yamlEventNext(yaml);

                    StringList *const valueList = strLstNew();

                    do
                    {
                        yamlEventCheck(dependDefVal, yamlEventTypeScalar);

                        strLstAdd(valueList, dependDefVal.value);

                        dependDefVal = yamlEventNext(yaml);
                    }
                    while (dependDefVal.type != yamlEventTypeSeqEnd);

                    optDependRaw.valueList = valueList;
                }
                else
                {
                    YamlEvent dependDefVal = yamlEventNext(yaml);
                    yamlEventCheck(dependDefVal, yamlEventTypeScalar);

                    if (strEqZ(dependDef.value, "default"))
                        optDependRaw.defaultValue = dependDefVal.value;
                    else if (strEqZ(dependDef.value, "option"))
                        optDependRaw.option = dependDefVal.value;
                    else
                        THROW_FMT(FormatError, "unknown depend definition '%s'", strZ(dependDef.value));
                }

                dependDef = yamlEventNext(yaml);
            }
            while (dependDef.type != yamlEventTypeMapEnd);

            MEM_CONTEXT_PRIOR_BEGIN()
            {
                BldCfgOptionDependRaw *optDepend = memNew(sizeof(BldCfgOptionDependRaw));

                *optDepend = (BldCfgOptionDependRaw)
                {
                    .option = strDup(optDependRaw.option),
                    .defaultValue = strDup(optDependRaw.defaultValue),
                    .valueList = strLstDup(optDependRaw.valueList)
                };

                result = optDepend;
            }
            MEM_CONTEXT_PRIOR_END();
        }
        else
        {
            // Else depend is inherited
            CHECK(AssertError, optList != NULL, "option list is NULL");
            yamlEventCheck(dependVal, yamlEventTypeScalar);

            const BldCfgOptionRaw *const optInherit = lstFind(optList, &dependVal.value);

            if (optInherit == NULL)
                THROW_FMT(FormatError, "dependency inherited from option '%s' before it is defined", strZ(dependVal.value));

            result = optInherit->depend;
        }
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

// Helper to reconcile depend
static const BldCfgOptionDepend *
bldCfgParseDependReconcile(
    const BldCfgOptionRaw *const optRaw, const BldCfgOptionDependRaw *const optDependRaw, const List *const optList)
{
    BldCfgOptionDepend *result = NULL;

    if (optDependRaw != NULL)
    {
        if (optDependRaw->defaultValue != NULL && !strEq(optRaw->type, OPT_TYPE_BOOLEAN_STR) &&
            !strEq(optRaw->type, OPT_TYPE_INTEGER_STR))
        {
            THROW_FMT(FormatError, "dependency default invalid for non integer/boolean option '%s'", strZ(optRaw->name));
        }

        const BldCfgOption *const optDepend = lstFind(optList, &optDependRaw->option);

        if (optDepend == NULL)
            THROW_FMT(FormatError, "dependency on undefined option '%s'", strZ(optDependRaw->option));

        result = memNew(sizeof(BldCfgOptionDepend));
        *result = (BldCfgOptionDepend)
        {
            .option = optDepend,
            .defaultValue = strDup(optDependRaw->defaultValue),
            .valueList = strLstDup(optDependRaw->valueList)
        };
    }

    return result;
}

// Helper to parse deprecate
static List *
bldCfgParseOptionDeprecate(Yaml *const yaml)
{
    List *result = lstNewP(sizeof(BldCfgOptionDeprecateRaw), .comparator = lstComparatorStr);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        yamlEventNextCheck(yaml, yamlEventTypeMapBegin);
        YamlEvent optDeprecate = yamlEventNext(yaml);

        do
        {
            yamlEventCheck(optDeprecate, yamlEventTypeScalar);
            const String *name = optDeprecate.value;
            bool indexed = false;

            yamlEventNextCheck(yaml, yamlEventTypeMapBegin);
            yamlEventNextCheck(yaml, yamlEventTypeMapEnd);

            // Determine if this deprecation is indexed
            const size_t questionPos = (size_t)strChr(name, '?');

            if (questionPos != (size_t)-1)
            {
                name = strNewFmt("%s%s", strZ(strSubN(name, 0, questionPos)), strZ(strSub(name, questionPos + 1)));
                indexed = true;
            }

            // Create final deprecation if it does not already exist
            BldCfgOptionDeprecateRaw *deprecate = lstFind(result, &name);

            if (deprecate == NULL)
            {
                MEM_CONTEXT_PRIOR_BEGIN()
                {
                    lstAdd(result, &(BldCfgOptionDeprecateRaw){.name = strDup(name)});
                }
                MEM_CONTEXT_PRIOR_END();

                deprecate = lstFind(result, &name);
                CHECK(AssertError, deprecate != NULL, "deprecate is NULL");
            }

            // Set indexed/unindexed flags
            if (indexed)
                deprecate->indexed = true;
            else
                deprecate->unindexed = true;

            optDeprecate = yamlEventNext(yaml);
        }
        while (optDeprecate.type != yamlEventTypeMapEnd);

        lstSort(result, sortOrderAsc);
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

// Helper to reconcile deprecate
static List *
bldCfgParseOptionDeprecateReconcile(const List *const optDeprecateRawList)
{
    List *result = NULL;

    if (optDeprecateRawList != NULL)
    {
        result = lstNewP(sizeof(BldCfgOptionDeprecate), .comparator = lstComparatorStr);

        for (unsigned int optDeprecateRawIdx = 0; optDeprecateRawIdx < lstSize(optDeprecateRawList); optDeprecateRawIdx++)
        {
            const BldCfgOptionDeprecateRaw *const optDeprecateRaw = lstGet(optDeprecateRawList, optDeprecateRawIdx);
            const BldCfgOptionDeprecate bldCfgOptionDeprecate =
            {
                .name = strDup(optDeprecateRaw->name),
                .indexed = optDeprecateRaw->indexed,
                .unindexed = optDeprecateRaw->unindexed,
            };

            lstAdd(result, &bldCfgOptionDeprecate);
        }
    }

    return result;
}

// Helper to parse the option command list
static const List *
bldCfgParseOptionCommandList(Yaml *const yaml, const List *const optList)
{
    const List *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        YamlEvent optCmdVal = yamlEventNext(yaml);

        // If command list is defined
        if (optCmdVal.type == yamlEventTypeMapBegin)
        {
            List *const optCmdRawList = lstNewP(sizeof(BldCfgOptionCommandRaw), .comparator = lstComparatorStr);

            YamlEvent optCmd = yamlEventNext(yaml);

            do
            {
                yamlEventCheck(optCmd, yamlEventTypeScalar);
                BldCfgOptionCommandRaw optCmdRaw = {.name = optCmd.value};

                yamlEventNextCheck(yaml, yamlEventTypeMapBegin);
                YamlEvent optCmdDef = yamlEventNext(yaml);

                if (optCmdDef.type == yamlEventTypeScalar)
                {
                    do
                    {
                        yamlEventCheck(optCmdDef, yamlEventTypeScalar);

                        if (strEqZ(optCmdDef.value, "allow-list"))
                        {
                            optCmdRaw.allowList = bldCfgParseAllowList(yaml, NULL);
                        }
                        else if (strEqZ(optCmdDef.value, "command-role"))
                        {
                            optCmdRaw.roleList = bldCfgParseCommandRole(yaml);
                        }
                        else if (strEqZ(optCmdDef.value, "depend"))
                        {
                            MEM_CONTEXT_BEGIN(lstMemContext(optCmdRawList))
                            {
                                optCmdRaw.depend = bldCfgParseDepend(yaml, optList);
                            }
                            MEM_CONTEXT_END();
                        }
                        else if (strEqZ(optCmdDef.value, "default"))
                        {
                            MEM_CONTEXT_BEGIN(lstMemContext(optCmdRawList))
                            {
                                optCmdRaw.defaultValue = bldCfgParseDefault(yaml);
                            }
                            MEM_CONTEXT_END();
                        }
                        else
                        {
                            YamlEvent optCmdDefVal = yamlEventNextCheck(yaml, yamlEventTypeScalar);

                            if (strEqZ(optCmdDef.value, "internal"))
                            {
                                optCmdRaw.internal = varNewBool(yamlBoolParse(optCmdDefVal));
                            }
                            else if (strEqZ(optCmdDef.value, "required"))
                            {
                                optCmdRaw.required = varNewBool(yamlBoolParse(optCmdDefVal));
                            }
                            else
                                THROW_FMT(FormatError, "unknown option command definition '%s'", strZ(optCmdDef.value));
                        }

                        optCmdDef = yamlEventNext(yaml);
                    }
                    while (optCmdDef.type != yamlEventTypeMapEnd);
                }
                else
                    yamlEventCheck(optCmdDef, yamlEventTypeMapEnd);

                MEM_CONTEXT_BEGIN(lstMemContext(optCmdRawList))
                {
                    const BldCfgOptionCommandRaw bldCfgOptionCommandRaw =
                    {
                        .name = strDup(optCmdRaw.name),
                        .internal = varDup(optCmdRaw.internal),
                        .required = varDup(optCmdRaw.required),
                        .defaultValue = optCmdRaw.defaultValue,
                        .depend = optCmdRaw.depend,
                        .allowList = bldCfgParseAllowListDup(optCmdRaw.allowList),
                        .roleList = strLstDup(optCmdRaw.roleList),
                    };

                    lstAdd(optCmdRawList, &bldCfgOptionCommandRaw);
                }
                MEM_CONTEXT_END();

                optCmd = yamlEventNext(yaml);
            }
            while (optCmd.type != yamlEventTypeMapEnd);

            lstSort(optCmdRawList, sortOrderAsc);

            result = lstMove(optCmdRawList, memContextPrior());
        }
        // Else command list is inherited
        else
        {
            CHECK(AssertError, optList != NULL, "option list is NULL");
            yamlEventCheck(optCmdVal, yamlEventTypeScalar);

            const BldCfgOptionRaw *const optInherit = lstFind(optList, &optCmdVal.value);
            CHECK(AssertError, optInherit != NULL, "inherited option is NULL");

            result = optInherit->cmdList;
        }
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

static List *
bldCfgParseOptionList(Yaml *const yaml, const List *const cmdList, const List *const optGrpList)
{
    List *const result = lstNewP(sizeof(BldCfgOption), .comparator = lstComparatorStr);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        List *const optListRaw = lstNewP(sizeof(BldCfgOptionRaw), .comparator = lstComparatorStr);

        // Parse raw
        // -------------------------------------------------------------------------------------------------------------------------
        yamlEventNextCheck(yaml, yamlEventTypeScalar);
        yamlEventNextCheck(yaml, yamlEventTypeMapBegin);

        YamlEvent opt = yamlEventNext(yaml);

        do
        {
            yamlEventCheck(opt, yamlEventTypeScalar);
            BldCfgOptionRaw optRaw = {.name = opt.value, .required = BOOL_TRUE_VAR};
            bool inheritFound = false;

            yamlEventNextCheck(yaml, yamlEventTypeMapBegin);

            YamlEvent optDef = yamlEventNext(yaml);

            do
            {
                yamlEventCheck(optDef, yamlEventTypeScalar);

                if (strEqZ(optDef.value, "allow-list"))
                {
                    optRaw.allowList = bldCfgParseAllowList(yaml, optListRaw);
                }
                else if (strEqZ(optDef.value, "allow-range"))
                {
                    optRaw.allowRange = bldCfgParseAllowRange(yaml);
                }
                else if (strEqZ(optDef.value, "command"))
                {
                    optRaw.cmdList = bldCfgParseOptionCommandList(yaml, optListRaw);
                }
                else if (strEqZ(optDef.value, "command-role"))
                {
                    optRaw.cmdRoleList = bldCfgParseCommandRole(yaml);
                }
                else if (strEqZ(optDef.value, "default"))
                {
                    optRaw.defaultValue = bldCfgParseDefault(yaml);
                }
                else if (strEqZ(optDef.value, "depend"))
                {
                    optRaw.depend = bldCfgParseDepend(yaml, optListRaw);
                }
                else if (strEqZ(optDef.value, "deprecate"))
                {
                    optRaw.deprecateList = bldCfgParseOptionDeprecate(yaml);
                }
                else
                {
                    YamlEvent optDefVal = yamlEventNextCheck(yaml, yamlEventTypeScalar);

                    if (strEqZ(optDef.value, "default-type"))
                    {
                        if (strEqZ(optDefVal.value, "quote"))
                            optRaw.defaultType = defaultTypeQuote;
                        else if (strEqZ(optDefVal.value, "literal"))
                            optRaw.defaultType = defaultTypeLiteral;
                        else if (strEqZ(optDefVal.value, "dynamic"))
                            optRaw.defaultType = defaultTypeDynamic;
                        else
                        {
                            THROW_FMT(
                                FormatError, "option '%s' has invalid default type '%s'", strZ(optRaw.name), strZ(optDefVal.value));
                        }
                    }
                    else if (strEqZ(optDef.value, "group"))
                    {
                        optRaw.group = optDefVal.value;

                        if (!lstExists(optGrpList, &optRaw.group))
                            THROW_FMT(FormatError, "option '%s' has invalid group '%s'", strZ(optRaw.name), strZ(optRaw.group));
                    }
                    else if (strEqZ(optDef.value, "inherit"))
                    {
                        const BldCfgOptionRaw *const optInherit = lstFind(optListRaw, &optDefVal.value);
                        CHECK(AssertError, optInherit != NULL, "inherited option is NULL");

                        optRaw = *optInherit;
                        optRaw.name = opt.value;

                        // Deprecations cannot be inherited
                        optRaw.deprecateList = NULL;

                        inheritFound = true;
                    }
                    else if (strEqZ(optDef.value, "internal"))
                    {
                        optRaw.internal = yamlBoolParse(optDefVal);
                    }
                    else if (strEqZ(optDef.value, "bool-like"))
                    {
                        optRaw.boolLike = yamlBoolParse(optDefVal);
                    }
                    else if (strEqZ(optDef.value, "beta"))
                    {
                        optRaw.beta = yamlBoolParse(optDefVal);
                    }
                    else if (strEqZ(optDef.value, "negate"))
                    {
                        optRaw.negate = varNewBool(yamlBoolParse(optDefVal));
                    }
                    else if (strEqZ(optDef.value, "required"))
                    {
                        optRaw.required = varNewBool(yamlBoolParse(optDefVal));
                    }
                    else if (strEqZ(optDef.value, "section"))
                    {
                        optRaw.section = optDefVal.value;
                    }
                    else if (strEqZ(optDef.value, "secure"))
                    {
                        optRaw.secure = yamlBoolParse(optDefVal);
                    }
                    else if (strEqZ(optDef.value, "type"))
                    {
                        optRaw.type = optDefVal.value;
                    }
                    else
                        THROW_FMT(FormatError, "unknown option definition '%s'", strZ(optDef.value));
                }

                optDef = yamlEventNext(yaml);
            }
            while (optDef.type != yamlEventTypeMapEnd);

            // Type is required
            if (optRaw.type == NULL)
                THROW_FMT(FormatError, "option '%s' requires 'type'", strZ(optRaw.name));

            // Set defaults if not inherited
            if (!inheritFound)
            {
                // Section defaults to command line
                if (optRaw.section == NULL)
                    optRaw.section = SECTION_COMMAND_LINE_STR;

                // Set negate default if not defined
                if (optRaw.negate == NULL)
                {
                    optRaw.negate = varNewBool(
                        (strEq(optRaw.type, OPT_TYPE_BOOLEAN_STR) || optRaw.boolLike) &&
                        !strEq(optRaw.section, SECTION_COMMAND_LINE_STR));
                }

                // Build default command list if not defined
                if (optRaw.cmdList == NULL)
                {
                    List *optCmdList = lstNewP(sizeof(BldCfgOptionCommandRaw), .comparator = lstComparatorStr);

                    for (unsigned int cmdIdx = 0; cmdIdx < lstSize(cmdList); cmdIdx++)
                    {
                        const BldCfgCommand *const cmd = lstGet(cmdList, cmdIdx);

                        if (!strEq(cmd->name, CMD_HELP_STR) && !strEq(cmd->name, CMD_VERSION_STR))
                            lstAdd(optCmdList, &(BldCfgOptionCommandRaw){.name = cmd->name});
                    }

                    lstSort(optCmdList, sortOrderAsc);
                    optRaw.cmdList = optCmdList;
                }
            }

            // Set reset
            optRaw.reset = !strEq(optRaw.section, SECTION_COMMAND_LINE_STR);

            lstAdd(optListRaw, &optRaw);

            opt = yamlEventNext(yaml);
        }
        while (opt.type != yamlEventTypeMapEnd);

        lstSort(optListRaw, sortOrderAsc);

        // Copy option list to result
        // -------------------------------------------------------------------------------------------------------------------------
        for (unsigned int optRawIdx = 0; optRawIdx < lstSize(optListRaw); optRawIdx++)
        {
            const BldCfgOptionRaw *const optRaw = lstGet(optListRaw, optRawIdx);

            MEM_CONTEXT_BEGIN(lstMemContext(result))
            {
                const BldCfgOption bldCfgOption =
                {
                    .name = strDup(optRaw->name),
                    .type = strDup(optRaw->type),
                    .section = strDup(optRaw->section),
                    .boolLike = optRaw->boolLike,
                    .internal = optRaw->internal,
                    .beta = optRaw->beta,
                    .required = varBool(optRaw->required),
                    .negate = varBool(optRaw->negate),
                    .reset = optRaw->reset,
                    .defaultType = optRaw->defaultType,
                    .defaultValue = bldCfgParseDefaultDup(optRaw->defaultValue),
                    .group = strDup(optRaw->group),
                    .secure = optRaw->secure,
                    .allowList = bldCfgParseAllowListDup(optRaw->allowList),
                    .allowRange = bldCfgParseAllowRangeDup(optRaw->allowRange),
                    .deprecateList = bldCfgParseOptionDeprecateReconcile(optRaw->deprecateList),
                };

                lstAdd(result, &bldCfgOption);
            }
            MEM_CONTEXT_END();
        }

        // Reconcile option list
        // -------------------------------------------------------------------------------------------------------------------------
        for (unsigned int optRawIdx = 0; optRawIdx < lstSize(optListRaw); optRawIdx++)
        {
            const BldCfgOptionRaw *const optRaw = lstGet(optListRaw, optRawIdx);

            // Reconcile option command list roles
            List *const cmdOptList = lstNewP(sizeof(BldCfgOptionCommand), .comparator = lstComparatorStr);

            for (unsigned int optCmdRawIdx = 0; optCmdRawIdx < lstSize(optRaw->cmdList); optCmdRawIdx++)
            {
                BldCfgOptionCommandRaw optCmd = *(BldCfgOptionCommandRaw *)lstGet(optRaw->cmdList, optCmdRawIdx);

                // Lookup command
                const BldCfgCommand *const cmd = lstFind(cmdList, &optCmd.name);

                if (cmd == NULL)
                {
                    THROW_FMT(
                        FormatError, "invalid command '%s' in option '%s' command list", strZ(optCmd.name), strZ(optRaw->name));
                }

                // Default required to option required if not defined
                if (optCmd.required == NULL)
                    optCmd.required = optRaw->required;

                // Default internal to option internal if not defined
                if (optCmd.internal == NULL)
                    optCmd.internal = varNewBool(optRaw->internal);

                // Default command role list if not defined
                if (optCmd.roleList == NULL)
                {
                    // Use option list as default if defined. It will need to be filtered against roles valid for each command
                    if (optRaw->cmdRoleList != NULL)
                    {
                        StringList *const roleList = strLstNew();

                        for (unsigned int cmdRoleIdx = 0; cmdRoleIdx < strLstSize(optRaw->cmdRoleList); cmdRoleIdx++)
                        {
                            const String *role = strLstGet(optRaw->cmdRoleList, cmdRoleIdx);

                            if (strLstExists(cmd->roleList, role))
                                strLstAdd(roleList, role);
                        }

                        optCmd.roleList = roleList;
                    }
                    // Else use option command list
                    else
                        optCmd.roleList = cmd->roleList;
                }

                CHECK(AssertError, optCmd.roleList != NULL, "role list is NULL");

                MEM_CONTEXT_BEGIN(lstMemContext(cmdOptList))
                {
                    BldCfgOptionCommand bldCfgOptionCommand =
                    {
                        .name = strDup(optCmd.name),
                        .internal = varBool(optCmd.internal),
                        .required = varBool(optCmd.required),
                        .defaultValue = bldCfgParseDefaultDup(optCmd.defaultValue),
                        .depend = bldCfgParseDependReconcile(optRaw, optCmd.depend, result),
                        .allowList = bldCfgParseAllowListDup(optCmd.allowList),
                        .roleList = strLstDup(optCmd.roleList),
                    };

                    lstAdd(cmdOptList, &bldCfgOptionCommand);
                }
                MEM_CONTEXT_END();
            }

            BldCfgOption *const opt = lstGet(result, optRawIdx);
            CHECK(AssertError, strEq(opt->name, optRaw->name), "option name does not equal raw option name");

            MEM_CONTEXT_BEGIN(lstMemContext(result))
            {
                opt->cmdList = lstMove(cmdOptList, memContextCurrent());
                opt->depend = bldCfgParseDependReconcile(optRaw, optRaw->depend, result);
            }
            MEM_CONTEXT_END();
        }
    }
    MEM_CONTEXT_TEMP_END();

    return result;
}

/***********************************************************************************************************************************
Build option resolve order list
***********************************************************************************************************************************/
static List *
bldCfgParseOptionResolveList(const List *const optList)
{
    List *const result = lstNewP(sizeof(BldCfgOption *), .comparator = lstComparatorStr);

    // The stanza option will always be resolved first since errors can be confusing when it is missing. That means it must exist
    // and cannot have any dependencies.
    // -----------------------------------------------------------------------------------------------------------------------------
    const BldCfgOption *const optStanza = lstFind(optList, &OPT_STANZA_STR);

    if (optStanza == NULL)
        THROW(FormatError, "option '" OPT_STANZA "' must exist");

    if (optStanza->depend != NULL)
        THROW(FormatError, "option '" OPT_STANZA "' may not depend on other option");

    for (unsigned int optCmdIdx = 0; optCmdIdx < lstSize(optStanza->cmdList); optCmdIdx++)
    {
        const BldCfgOptionCommand *const optStanzaCmd = lstGet(optStanza->cmdList, optCmdIdx);

        if (optStanzaCmd->depend != NULL)
            THROW_FMT(FormatError, "option '" OPT_STANZA "' command '%s' may not depend on other option", strZ(optStanzaCmd->name));
    }

    // Determine resolve order
    // -----------------------------------------------------------------------------------------------------------------------------
    StringList *const optResolveList = strLstNew();
    strLstAdd(optResolveList, OPT_STANZA_STR);

    do
    {
        // Was at least one option resolved in the loop?
        bool resolved = false;

        for (unsigned int optIdx = 0; optIdx < lstSize(optList); optIdx++)
        {
            const BldCfgOption *const opt = lstGet(optList, optIdx);

            // If the option has already been resolved then continue
            if (strLstExists(optResolveList, opt->name))
                continue;

            // If the option dependency has not been resolved then continue
            if (opt->depend != NULL && !strLstExists(optResolveList, opt->depend->option->name))
                continue;

            // If the option command dependency has not been resolved then continue
            unsigned int optCmdIdx = 0;

            for (; optCmdIdx < lstSize(opt->cmdList); optCmdIdx++)
            {
                const BldCfgOptionCommand *const optCmd = lstGet(opt->cmdList, optCmdIdx);

                if (optCmd->depend != NULL && !strLstExists(optResolveList, optCmd->depend->option->name))
                    break;
            }

            if (optCmdIdx < lstSize(opt->cmdList))
                continue;

            // Option dependencies have been resolved
            strLstAdd(optResolveList, opt->name);
            resolved = true;
        }

        // If nothing was resolved in the loop then there may be a circular reference
        if (!resolved)
        {
            // Find the options that were not resolved
            StringList *const unresolvedList = strLstNew();

            for (unsigned int optIdx = 0; optIdx < lstSize(optList); optIdx++)
            {
                const BldCfgOption *const opt = lstGet(optList, optIdx);

                if (!strLstExists(optResolveList, opt->name))
                    strLstAdd(unresolvedList, opt->name);
            }

            THROW_FMT(
                FormatError,
                "unable to resolve dependencies for option(s) '%s'\n"
                "HINT: are there circular dependencies?",
                strZ(strLstJoin(unresolvedList, ", ")));
        }
    }
    while (strLstSize(optResolveList) != lstSize(optList));

    // Copy resolved list
    for (unsigned int optResolveIdx = 0; optResolveIdx < strLstSize(optResolveList); optResolveIdx++)
    {
        const String *const optName = strLstGet(optResolveList, optResolveIdx);
        const BldCfgOption *const opt = lstFind(optList, &optName);
        lstAdd(result, &opt);
    }

    return result;
}

/**********************************************************************************************************************************/
BldCfg
bldCfgParse(const Storage *const storageRepo)
{
    // Initialize yaml
    Yaml *const yaml = yamlNew(storageGetP(storageNewReadP(storageRepo, STRDEF("src/build/config/config.yaml"))));
    yamlEventNextCheck(yaml, yamlEventTypeMapBegin);

    // Parse configuration
    const List *const cmdList = bldCfgParseCommandList(yaml);
    const List *const optGrpList = bldCfgParseOptionGroupList(yaml);
    const List *const optList = bldCfgParseOptionList(yaml, cmdList, optGrpList);
    const List *const optResolveList = bldCfgParseOptionResolveList(optList);

    return (BldCfg){.cmdList = cmdList, .optGrpList = optGrpList, .optList = optList, .optResolveList = optResolveList};
}
