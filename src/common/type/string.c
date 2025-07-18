/***********************************************************************************************************************************
String Handler
***********************************************************************************************************************************/
#include "build.auto.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/debug.h"
#include "common/macro.h"
#include "common/memContext.h"
#include "common/type/string.h"
#include "common/type/stringList.h"

/***********************************************************************************************************************************
Constant strings that are generally useful
***********************************************************************************************************************************/
STRING_EXTERN(CR_STR,                                               "\r");
STRING_EXTERN(CRLF_STR,                                             "\r\n");
STRING_EXTERN(DOT_STR,                                              ".");
STRING_EXTERN(DOTDOT_STR,                                           "..");
STRING_EXTERN(EMPTY_STR,                                            "");
STRING_EXTERN(FALSE_STR,                                            FALSE_Z);
STRING_EXTERN(FSLASH_STR,                                           "/");
STRING_EXTERN(LF_STR,                                               "\n");
STRING_EXTERN(N_STR,                                                "n");
STRING_EXTERN(NULL_STR,                                             NULL_Z);
STRING_EXTERN(TRUE_STR,                                             TRUE_Z);
STRING_EXTERN(Y_STR,                                                "y");
STRING_EXTERN(ZERO_STR,                                             "0");

/***********************************************************************************************************************************
Buffer macros
***********************************************************************************************************************************/
// Fixed size buffer allocated at the end of the object allocation
#define STR_FIXED_BUFFER                                            (char *)(this + 1)

// Is the string using the fixed size buffer?
#define STR_IS_FIXED_BUFFER()                                       (this->pub.buffer == STR_FIXED_BUFFER)

// Empty buffer
#define STR_EMPTY_BUFFER                                            (EMPTY_STR->pub.buffer)

// Is the string using the empty buffer?
#define STR_IS_EMPTY_BUFFER()                                       (this->pub.buffer == STR_EMPTY_BUFFER)

/***********************************************************************************************************************************
Maximum size of a string
***********************************************************************************************************************************/
#define STRING_SIZE_MAX                                            1073741824

#define CHECK_SIZE(size)                                                                                                           \
    do                                                                                                                             \
    {                                                                                                                              \
        if ((size) > STRING_SIZE_MAX)                                                                                              \
            THROW(AssertError, "string size must be <= " STRINGIFY(STRING_SIZE_MAX) " bytes");                                     \
    }                                                                                                                              \
    while (0)

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct String
{
    StringPub pub;                                                  // Publicly accessible variables
};

/**********************************************************************************************************************************/
FN_EXTERN String *
strNew(void)
{
    FUNCTION_TEST_VOID();

    OBJ_NEW_BEGIN(String, .allocQty = 1)
    {
        *this = (String)
        {
            .pub =
            {
                // Set empty so nothing is allocated until needed
                .buffer = STR_EMPTY_BUFFER,
            },
        };
    }
    OBJ_NEW_END();

    FUNCTION_TEST_RETURN(STRING, this);
}

/***********************************************************************************************************************************
Create fixed size String
***********************************************************************************************************************************/
static String *
strNewFixed(const size_t size)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(SIZE, size);
    FUNCTION_TEST_END();

    CHECK_SIZE(size);

    // If the string is larger than the extra allowed with a mem context then allocate the buffer separately
    const size_t allocExtra = sizeof(String) + size + 1;

    if (allocExtra > MEM_CONTEXT_ALLOC_EXTRA_MAX)
    {
        OBJ_NEW_BEGIN(String, .allocQty = 1)
        {
            *this = (String)
            {
                .pub =
                {
                    .size = (unsigned int)size,
                    .buffer = memNew(size + 1),
                },
            };
        }
        OBJ_NEW_END();

        FUNCTION_TEST_RETURN(STRING, this);
    }

    OBJ_NEW_EXTRA_BEGIN(String, (uint16_t)(allocExtra))
    {
        *this = (String)
        {
            .pub =
            {
                .size = (unsigned int)size,
                .buffer = STR_FIXED_BUFFER,
            },
        };
    }
    OBJ_NEW_END();

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewZ(const char *const string)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, string);
    FUNCTION_TEST_END();

    ASSERT(string != NULL);

    // Create object
    String *const this = strNewFixed(strlen(string));

    // Assign string
    memcpy(this->pub.buffer, string, strSize(this));
    this->pub.buffer[strSize(this)] = '\0';

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewDiv(const uint64_t dividend, const uint64_t divisor, StrNewDivParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(UINT64, dividend);
        FUNCTION_TEST_PARAM(UINT64, divisor);
        FUNCTION_TEST_PARAM(UINT, param.precision);
        FUNCTION_TEST_PARAM(BOOL, param.trim);
    FUNCTION_TEST_END();

    char working[CVT_DIV_BUFFER_SIZE];

    size_t resultSize = cvtDivToZ(dividend, divisor, param.precision, param.trim, working, sizeof(working));

    FUNCTION_TEST_RETURN(STRING, strNewZN(working, resultSize));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewPct(const uint64_t dividend, const uint64_t divisor)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(UINT64, dividend);
        FUNCTION_TEST_PARAM(UINT64, divisor);
    FUNCTION_TEST_END();

    char working[CVT_PCT_BUFFER_SIZE];

    size_t resultSize = cvtPctToZ(dividend, divisor, working, sizeof(working));

    FUNCTION_TEST_RETURN(STRING, strNewZN(working, resultSize));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewTime(const char *const format, const time_t timestamp, const StrNewTimeParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, format);
        FUNCTION_TEST_PARAM(TIME, timestamp);
        FUNCTION_TEST_PARAM(BOOL, param.utc);
    FUNCTION_TEST_END();

    char buffer[64];

    // We can ignore this warning here since the format parameter of strNewTimeP() is checked
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    cvtTimeToZP(format, timestamp, buffer, sizeof(buffer), .utc = param.utc);
#pragma GCC diagnostic pop

    FUNCTION_TEST_RETURN(STRING, strNewZ(buffer));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewBuf(const Buffer *const buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(buffer != NULL);

    // Create object
    String *const this = strNewFixed(bufUsed(buffer));

    // Assign string
    if (strSize(this) != 0)
        memcpy(this->pub.buffer, bufPtrConst(buffer), strSize(this));

    this->pub.buffer[strSize(this)] = 0;

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewEncode(const EncodingType type, const Buffer *const buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, type);
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(buffer != NULL);

    // Create object
    String *const this = strNewFixed(encodeToStrSize(type, bufUsed(buffer)));

    // Encode buffer
    if (bufUsed(buffer) > 0)
    {
        encodeToStr(type, bufPtrConst(buffer), bufUsed(buffer), this->pub.buffer);
    }
    // Else zero-terminate
    else
        this->pub.buffer[0] = '\0';

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewFmt(const char *const format, ...)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRINGZ, format);
    FUNCTION_TEST_END();

    ASSERT(format != NULL);

    // Determine how long the allocated string needs to be and create object
    va_list argumentList;
    va_start(argumentList, format);
    String *const this = strNewFixed((size_t)vsnprintf(NULL, 0, format, argumentList));
    va_end(argumentList);

    // Format string
    va_start(argumentList, format);
    vsnprintf(this->pub.buffer, strSize(this) + 1, format, argumentList);
    va_end(argumentList);

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strNewZN(const char *const string, const size_t size)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM_P(CHARDATA, string);
        FUNCTION_TEST_PARAM(SIZE, size);
    FUNCTION_TEST_END();

    ASSERT(string != NULL);

    // Create object
    String *const this = strNewFixed(size);

    // Assign string
    memcpy(this->pub.buffer, string, strSize(this));
    this->pub.buffer[strSize(this)] = 0;

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strBase(const String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(STRING, strNewZ(strBaseZ(this)));
}

FN_EXTERN const char *
strBaseZ(const String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    const char *end = this->pub.buffer + strSize(this);

    while (end > this->pub.buffer && *(end - 1) != '/')
        end--;

    FUNCTION_TEST_RETURN_CONST(STRINGZ, end);
}

/**********************************************************************************************************************************/
FN_EXTERN bool
strBeginsWith(const String *const this, const String *const beginsWith)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRING, beginsWith);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(beginsWith != NULL);

    FUNCTION_TEST_RETURN(BOOL, strBeginsWithZ(this, strZ(beginsWith)));
}

FN_EXTERN bool
strBeginsWithZ(const String *const this, const char *const beginsWith)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, beginsWith);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(beginsWith != NULL);

    bool result = false;
    const unsigned int beginsWithSize = (unsigned int)strlen(beginsWith);

    if (strSize(this) >= beginsWithSize)
        result = strncmp(strZ(this), beginsWith, beginsWithSize) == 0;

    FUNCTION_TEST_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
Resize the string to allow the requested number of characters to be appended
***********************************************************************************************************************************/
static void
strResize(String *const this, const size_t requested)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(SIZE, requested);
    FUNCTION_TEST_END();

    if (requested > this->pub.extra)
    {
        // Fixed size strings may not be resized
        CHECK(AssertError, !STR_IS_FIXED_BUFFER(), "resize of fixed size string");

        // Check size
        CHECK_SIZE(strSize(this) + requested);

        // Calculate new extra needs to satisfy request and leave extra space for new growth
        this->pub.extra = (unsigned int)(requested + ((strSize(this) + requested) / 2));

        // Adding too little extra space usually leads to immediate resizing so enforce a minimum
        if (this->pub.extra < STRING_EXTRA_MIN)
            this->pub.extra = STRING_EXTRA_MIN;

        MEM_CONTEXT_OBJ_BEGIN(this)
        {
            if (STR_IS_EMPTY_BUFFER())
                this->pub.buffer = memNew(strSize(this) + this->pub.extra + 1);
            else
                this->pub.buffer = memResize(this->pub.buffer, strSize(this) + this->pub.extra + 1);
        }
        MEM_CONTEXT_OBJ_END();
    }

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strCat(String *const this, const String *const cat)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRING, cat);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(cat != NULL);

    FUNCTION_TEST_RETURN(STRING, strCatZN(this, strZ(cat), strSize(cat)));
}

FN_EXTERN String *
strCatZ(String *const this, const char *const cat)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, cat);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(cat != NULL);

    // Determine length of string to append
    const size_t sizeGrow = strlen(cat);

    if (sizeGrow != 0)
    {
        // Ensure there is enough space to grow the string
        strResize(this, sizeGrow);

        // Append the string
        strcpy(this->pub.buffer + strSize(this), cat);
        this->pub.size += (unsigned int)sizeGrow;
        this->pub.extra -= (unsigned int)sizeGrow;
    }

    FUNCTION_TEST_RETURN(STRING, this);
}

FN_EXTERN String *
strCatZN(String *const this, const char *const cat, const size_t size)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, cat);
        FUNCTION_TEST_PARAM(SIZE, size);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (size != 0)
    {
        ASSERT(cat != NULL);

        // Ensure there is enough space to grow the string
        strResize(this, size);

        // Append the string
        memcpy(this->pub.buffer + strSize(this), cat, size);
        this->pub.buffer[strSize(this) + size] = '\0';

        // Update size/extra
        this->pub.size += (unsigned int)size;
        this->pub.extra -= (unsigned int)size;
    }

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strCatBuf(String *const this, const Buffer *const buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(buffer != NULL);

    FUNCTION_TEST_RETURN(STRING, strCatZN(this, (const char *)bufPtrConst(buffer), bufUsed(buffer)));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strCatChr(String *const this, const char cat)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(CHAR, cat);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(cat != 0);

    // Ensure there is enough space to grow the string
    strResize(this, 1);

    // Append the character
    this->pub.buffer[this->pub.size++] = cat;
    this->pub.buffer[this->pub.size] = 0;
    this->pub.extra--;

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strCatEncode(String *const this, const EncodingType type, const Buffer *const buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(ENUM, type);
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(buffer != NULL);

    const size_t encodeSize = encodeToStrSize(type, bufUsed(buffer));

    if (encodeSize != 0)
    {
        // Ensure there is enough space to grow the string
        strResize(this, encodeSize);

        // Append the encoded string
        encodeToStr(type, bufPtrConst(buffer), bufUsed(buffer), this->pub.buffer + strSize(this));

        // Update size/extra
        this->pub.size += (unsigned int)encodeSize;
        this->pub.extra -= (unsigned int)encodeSize;
    }

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strCatTime(String *const this, const char *const format, const time_t timestamp, const StrCatTimeParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, format);
        FUNCTION_TEST_PARAM(TIME, timestamp);
        FUNCTION_TEST_PARAM(BOOL, param.utc);
    FUNCTION_TEST_END();

    char buffer[64];

    // We can ignore this warning here since the format parameter of strCatTimeP() is checked
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    cvtTimeToZP(format, timestamp, buffer, sizeof(buffer), .utc = param.utc);
#pragma GCC diagnostic pop

    FUNCTION_TEST_RETURN(STRING, strCatZ(this, buffer));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strCatFmt(String *const this, const char *const format, ...)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, format);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(format != NULL);

    // Determine how long the allocated string needs to be
    va_list argumentList;
    va_start(argumentList, format);
    const size_t sizeGrow = (size_t)vsnprintf(NULL, 0, format, argumentList);
    va_end(argumentList);

    if (sizeGrow != 0)
    {
        // Ensure there is enough space to grow the string
        strResize(this, sizeGrow);

        // Append the formatted string
        va_start(argumentList, format);
        vsnprintf(this->pub.buffer + strSize(this), sizeGrow + 1, format, argumentList);
        va_end(argumentList);

        this->pub.size += (unsigned int)sizeGrow;
        this->pub.extra -= (unsigned int)sizeGrow;
    }

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN int
strCmp(const String *const this, const String *const compare)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRING, compare);
    FUNCTION_TEST_END();

    if (this != NULL && compare != NULL)
        FUNCTION_TEST_RETURN(INT, strcmp(strZ(this), strZ(compare)));
    else if (this == NULL)
    {
        if (compare == NULL)
            FUNCTION_TEST_RETURN(INT, 0);

        FUNCTION_TEST_RETURN(INT, -1);
    }

    FUNCTION_TEST_RETURN(INT, 1);
}

FN_EXTERN int
strCmpZ(const String *const this, const char *const compare)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, compare);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RETURN(INT, strCmp(this, compare == NULL ? NULL : STR(compare)));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strDup(const String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    String *result = NULL;

    if (this != NULL)
        result = strNewZ(strZ(this));

    FUNCTION_TEST_RETURN(STRING, result);
}

/**********************************************************************************************************************************/
FN_EXTERN bool
strEmpty(const String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RETURN(BOOL, strSize(this) == 0);
}

/**********************************************************************************************************************************/
FN_EXTERN bool
strEndsWith(const String *const this, const String *const endsWith)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRING, endsWith);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(endsWith != NULL);

    FUNCTION_TEST_RETURN(BOOL, strEndsWithZ(this, strZ(endsWith)));
}

FN_EXTERN bool
strEndsWithZ(const String *const this, const char *const endsWith)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, endsWith);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(endsWith != NULL);

    bool result = false;
    const unsigned int endsWithSize = (unsigned int)strlen(endsWith);

    if (strSize(this) >= endsWithSize)
        result = strcmp(strZ(this) + (strSize(this) - endsWithSize), endsWith) == 0;

    FUNCTION_TEST_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
There are two separate implementations because string objects can get the size very efficiently whereas the zero-terminated strings
would need a call to strlen().
***********************************************************************************************************************************/
FN_EXTERN bool
strEq(const String *const this, const String *const compare)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRING, compare);
    FUNCTION_TEST_END();

    bool result = false;

    if (this != NULL && compare != NULL)
    {
        if (strSize(this) == strSize(compare))
            result = strcmp(strZ(this), strZ(compare)) == 0;
    }
    else
        result = this == NULL && compare == NULL;

    FUNCTION_TEST_RETURN(BOOL, result);
}

FN_EXTERN bool
strEqZ(const String *const this, const char *const compare)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRINGZ, compare);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(compare != NULL);

    FUNCTION_TEST_RETURN(BOOL, strcmp(strZ(this), compare) == 0);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strFirstUpper(String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (strSize(this) > 0)
        this->pub.buffer[0] = (char)toupper(this->pub.buffer[0]);

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strFirstLower(String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (strSize(this) > 0)
        this->pub.buffer[0] = (char)tolower(this->pub.buffer[0]);

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strLower(String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    for (size_t idx = 0; idx < strSize(this); idx++)
        this->pub.buffer[idx] = (char)tolower(this->pub.buffer[idx]);

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strPath(const String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    const char *end = this->pub.buffer + strSize(this);

    while (end > this->pub.buffer && *(end - 1) != '/')
        end--;

    FUNCTION_TEST_RETURN(
        STRING,
        strNewZN(
            this->pub.buffer,
            end - this->pub.buffer <= 1 ? (size_t)(end - this->pub.buffer) : (size_t)(end - this->pub.buffer - 1)));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strPathAbsolute(const String *const this, const String *const base)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(STRING, base);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    String *result = NULL;

    // Path is already absolute so just return it
    if (strBeginsWith(this, FSLASH_STR))
    {
        result = strDup(this);
    }
    // Else we'll need to construct the absolute path. You would hope we could use realpath() here but it is so broken in the Posix
    // spec that is seems best avoided.
    else
    {
        ASSERT(base != NULL);

        // Base must be absolute to start
        if (!strBeginsWith(base, FSLASH_STR))
            THROW_FMT(AssertError, "base path '%s' is not absolute", strZ(base));

        MEM_CONTEXT_TEMP_BEGIN()
        {
            StringList *const baseList = strLstNewSplit(base, FSLASH_STR);
            StringList *const pathList = strLstNewSplit(this, FSLASH_STR);

            while (!strLstEmpty(pathList))
            {
                const String *const pathPart = strLstGet(pathList, 0);

                // If the last part is empty
                if (strSize(pathPart) == 0)
                {
                    // Allow when this is the last part since it just means there was a trailing /
                    if (strLstSize(pathList) == 1)
                    {
                        strLstRemoveIdx(pathList, 0);
                        break;
                    }

                    THROW_FMT(AssertError, "'%s' is not a valid relative path", strZ(this));
                }

                if (strEq(pathPart, DOTDOT_STR))
                {
                    const String *const basePart = strLstGet(baseList, strLstSize(baseList) - 1);

                    if (strSize(basePart) == 0)
                        THROW_FMT(AssertError, "relative path '%s' goes back too far in base path '%s'", strZ(this), strZ(base));

                    strLstRemoveIdx(baseList, strLstSize(baseList) - 1);
                }
                else if (!strEq(pathPart, DOT_STR))
                    strLstAdd(baseList, pathPart);

                strLstRemoveIdx(pathList, 0);
            }

            MEM_CONTEXT_PRIOR_BEGIN()
            {
                if (strLstSize(baseList) == 1)
                    result = strDup(FSLASH_STR);
                else
                    result = strLstJoin(baseList, "/");
            }
            MEM_CONTEXT_PRIOR_END();
        }
        MEM_CONTEXT_TEMP_END();
    }

    // There should not be any stray .. or // in the final result
    if (strstr(strZ(result), "/..") != NULL || strstr(strZ(result), "//") != NULL)
        THROW_FMT(AssertError, "result path '%s' is not absolute", strZ(result));

    FUNCTION_TEST_RETURN(STRING, result);
}

/**********************************************************************************************************************************/
FN_EXTERN const char *
strZNull(const String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    FUNCTION_TEST_RETURN_CONST(STRINGZ, this == NULL ? NULL : strZ(this));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strReplaceChr(String *const this, const char find, const char replace)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(CHAR, find);
        FUNCTION_TEST_PARAM(CHAR, replace);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    for (size_t stringIdx = 0; stringIdx < strSize(this); stringIdx++)
    {
        if (this->pub.buffer[stringIdx] == find)
            this->pub.buffer[stringIdx] = replace;
    }

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strSub(const String *const this, const size_t start)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(SIZE, start);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(start <= this->pub.size);

    FUNCTION_TEST_RETURN(STRING, strSubN(this, start, strSize(this) - start));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strSubN(const String *const this, const size_t start, const size_t size)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(SIZE, start);
        FUNCTION_TEST_PARAM(SIZE, size);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(start <= strSize(this));
    ASSERT(start + size <= strSize(this));

    FUNCTION_TEST_RETURN(STRING, strNewZN(strZ(this) + start, size));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strTrim(String *const this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Nothing to trim if size is zero
    if (strSize(this) > 0)
    {
        // Find the beginning of the string skipping all whitespace
        char *begin = this->pub.buffer;

        while (*begin != 0 && (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n'))
            begin++;

        // Find the end of the string skipping all whitespace
        char *end = this->pub.buffer + (strSize(this) - 1);

        while (end > begin && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
            end--;

        // Is there anything to trim?
        const size_t newSize = (size_t)(end - begin + 1);

        if (begin != this->pub.buffer || newSize < strSize(this))
        {
            // Calculate new size and extra
            this->pub.extra = (unsigned int)(strSize(this) - newSize);
            this->pub.size = (unsigned int)newSize;

            // Move the substr to the beginning of the buffer
            memmove(this->pub.buffer, begin, strSize(this));
            this->pub.buffer[strSize(this)] = 0;
        }
    }

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN int
strChr(const String *const this, const char chr)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
        FUNCTION_TEST_PARAM(CHAR, chr);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    int result = -1;

    if (strSize(this) > 0)
    {
        const char *const ptr = strchr(this->pub.buffer, chr);

        if (ptr != NULL)
            result = (int)(ptr - this->pub.buffer);
    }

    FUNCTION_TEST_RETURN(INT, result);
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strTruncIdx(String *const this, const int idx)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(idx >= 0 && (size_t)idx <= strSize(this));

    if (strSize(this) > 0)
    {
        // Reset the size to end at the index
        this->pub.extra = (unsigned int)(strSize(this) - (size_t)idx);
        this->pub.size = (unsigned int)idx;
        this->pub.buffer[strSize(this)] = 0;
    }

    FUNCTION_TEST_RETURN(STRING, this);
}

/**********************************************************************************************************************************/
FN_EXTERN void
strToLog(const String *const this, StringStatic *const debugLog)
{
    strStcFmt(debugLog, "{\"%s\"}", strZ(this));
}

/**********************************************************************************************************************************/
FN_EXTERN String *
strSizeFormat(const uint64_t size)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(UINT64, size);
    FUNCTION_TEST_END();

    String *result;

    if (size < 1024)
        result = strNewFmt("%" PRIu64 "B", size);
    else
    {
        char working[CVT_DIV_BUFFER_SIZE];
        uint64_t divisor = 1024 * 1024 * 1024;
        unsigned int precision = 1;
        const char *suffix = "GB";

        if (size < (1024 * 1024))
        {
            divisor = 1024;
            suffix = "KB";
        }
        else if (size < (1024 * 1024 * 1024))
        {
            divisor = 1024 * 1024;
            suffix = "MB";
        }

        // Skip precision when it would cause overflow
        if (size > UINT64_MAX / 10)
            precision = 0;

        // Format size
        cvtDivToZ(size, divisor, precision, true, working, sizeof(working));
        result = strNewFmt("%s%s", working, suffix);
    }

    FUNCTION_TEST_RETURN(STRING, result);
}
