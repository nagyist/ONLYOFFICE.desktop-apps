#include "translator.h"
#include <sstream>
#ifdef _WIN32
# include "platform_win/resource.h"
# include "platform_win/utils.h"
# include <Windows.h>
# include <codecvt>
# include <cwctype>
# define istalnum(c) std::iswalnum(c)
# define istalpha(c) std::iswalpha(c)
# define tistringstream std::wistringstream
#else
# include "platform_linux/utils.h"
# include "res/gresource.c"
# include <cctype>
# define istalnum(c) std::isalnum(c)
# define istalpha(c) std::isalpha(c)
# define tistringstream std::istringstream
#endif


bool isSeparator(tchar c)
{
    return c == _T(' ') || c == _T('\t') || c == _T('\r') || c == _T('\n');
}

bool isValidStringIdCharacter(tchar c)
{
    return istalnum(c) || istalpha(c) || c == _T('_');
}

bool isValidLocaleCharacter(tchar c)
{
    return istalpha(c) || c == _T('_');
}

tstring getPrimaryLang(const tstring &lang, bool withScript = false)
{
    if (lang.empty()) {
        NS_Logger::WriteLog(_T("An error occurred: ") + FUNCTION_INFO);
        return _T("en");
    }
    tistringstream iss(lang);
    tstring primlang, script;
    std::getline(iss, primlang, _T('_'));
    if (primlang.length() == 2 || primlang.length() == 3) {
        if (!withScript)
            return primlang;
        std::getline(iss, script, _T('_'));
        return (script.length() == 4) ? primlang + _T("_") + script : primlang;
    }
    NS_Logger::WriteLog(_T("An error occurred: ") + FUNCTION_INFO);
    return _T("en");
}

#ifdef _WIN32
wstring StrToWStr(const string &str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(str);
}
#endif

TranslationsMap Translator::translMap = TranslationsMap();
tstring Translator::langName = _T("en");
bool Translator::is_translations_valid = false;

#ifdef _WIN32
Translator::Translator(const tstring &lang, int resourceId)
#else
Translator::Translator(const tstring &lang, const char *resourcePath)
#endif
{
    langName = lang;
    NS_Logger::WriteLog(_T("Current locale: ") + langName);

#ifdef _WIN32
    HMODULE hInst = GetModuleHandle(NULL);
    if (HRSRC hRes = FindResource(hInst, MAKEINTRESOURCE(resourceId), RT_RCDATA)) {
        if (HGLOBAL hResData = LoadResource(hInst, hRes)) {
            if (LPVOID pData = LockResource(hResData)) {
                DWORD dataSize = SizeofResource(hInst, hRes);
                if (dataSize > 0) {
                    string text((const char*)pData, dataSize);
                    translations = StrToWStr(text);
                } else
                    NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
            } else
                NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
            FreeResource(hResData);
        } else
            NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
    } else
        NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
#else
    if (GResource *res = gresource_get_resource()) {
        g_resources_register(res);
        if (GBytes *bytes = g_resource_lookup_data(res, resourcePath, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL)) {
            gsize dataSize = 0;
            const char *pData = (const char*)g_bytes_get_data(bytes, &dataSize);
            if (dataSize > 0) {
                string text(pData, dataSize);
                translations = text;
            } else
                NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
            g_bytes_unref(bytes);
        } else
            NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
        g_resource_unref(res);
    } else
        NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
#endif

    if (!translations.empty()) {
        parseTranslations();
        if (!is_translations_valid)
            NS_Logger::WriteLog(_T("Cannot parse translations, error in string: ") + error_substr + _T(" <---"));
    } else
        NS_Logger::WriteLog(_T("Error: translations is empty."));
}

Translator::~Translator()
{

}

tstring Translator::tr(const char *str)
{
#ifdef _WIN32
    tstring translatedStr = StrToWStr(str);
#else
    tstring translatedStr = str;
#endif
    if (is_translations_valid) {
        for (auto &strIdPair : translMap) {
            //LocaleMap locMap = strIdPair.second;
            // for (LocaleMap::const_iterator it = strIdPair.second.begin(); it != strIdPair.second.end(); ++it) {
                //wcout << L"\n\n" << translatedStr << L"\n" << it->second;
                if (strIdPair.first == translatedStr) {
                    if (strIdPair.second.find(langName) != strIdPair.second.end())
                        return strIdPair.second[langName];
                    else {
                        tstring primaryLangAndScript = getPrimaryLang(langName, true);
                        if (strIdPair.second.find(primaryLangAndScript) != strIdPair.second.end())
                            return strIdPair.second[primaryLangAndScript];
                        else {
                            tstring primaryLang = getPrimaryLang(langName);
                            if (strIdPair.second.find(primaryLang) != strIdPair.second.end())
                                return strIdPair.second[primaryLang];
                        }
                    }
                    if (strIdPair.second.find(_T("en")) != strIdPair.second.end())
                        return strIdPair.second[_T("en")];
                    break;
                }
            // }
        }
    }
    return translatedStr;
}

void Translator::setLanguage(const tstring &lang)
{
    langName = lang;
    NS_Logger::WriteLog(_T("Current locale: ") + langName);
}

void Translator::parseTranslations()
{
    int token = TOKEN_BEGIN_DOCUMENT;
    tstring stringId, currentLocale;
    size_t pos = 0, len = translations.length();
    while (pos < len) {
        size_t incr = 1;
        tchar ch = translations.at(pos);

        switch (token) {
        case TOKEN_BEGIN_DOCUMENT:
        case TOKEN_END_VALUE:
            if (!isSeparator(ch)) {
                if (ch == _T(';')) {
                    // string is comment
                    size_t end = translations.find_first_of(_T('\n'), pos);
                    incr = (end == tstring::npos) ? len - pos : end - pos + 1;
                } else {
                    size_t end;
                    for (end = pos; end < len; end++) {
                        tchar c = translations.at(end);
                        if (!isValidLocaleCharacter(c))
                            break;
                    }
                    size_t locale_len = end - pos;
                    if (locale_len < 12 && locale_len != 0 && locale_len != 1 && locale_len != 4 && locale_len != 9) {
                        token = TOKEN_BEGIN_LOCALE;
                        continue;
                    } else {
                        // TOKEN_ERROR
                        error_substr = translations.substr(0, pos + 1);
                        return;
                    }
                }
            }
            break;

        case TOKEN_BEGIN_STRING_ID:
            if (!isSeparator(ch)) {
                size_t end;
                tchar c;
                for (end = pos; end < len; end++) {
                    c = translations.at(end);
                    if (!isValidStringIdCharacter(c))
                        break;
                }
                c = translations.at(end);
                if (end < len && !isSeparator(c) && c != _T('=')) {
                    // TOKEN_ERROR
                    error_substr = translations.substr(0, end + 1);
                    return;
                }
                stringId = translations.substr(pos, end - pos);
                if (!stringId.empty() && translMap.find(stringId) == translMap.end())
                    translMap[stringId] = LocaleMap();

                token = TOKEN_END_STRING_ID;
                incr = end - pos;
            }
            break;

        case TOKEN_END_STRING_ID:
            if (!isSeparator(ch)) {
                if (ch == _T('=')) {
                    token = TOKEN_BEGIN_VALUE;
                } else {
                    // TOKEN_ERROR
                    error_substr = translations.substr(0, pos + 1);
                    return;
                }
            }
            break;

        case TOKEN_BEGIN_LOCALE: {
            size_t end;
            for (end = pos; end < len; end++) {
                tchar c = translations.at(end);
                if (!isValidLocaleCharacter(c))
                    break;
            }
            size_t locale_len = end - pos;
            currentLocale = translations.substr(pos, locale_len);
            if (pos + locale_len == len) {
                error_substr = translations.substr(0, pos + locale_len);
                return;
            }
            token = TOKEN_END_LOCALE;
            incr = locale_len;
            break;
        }

        case TOKEN_END_LOCALE:
            if (!isSeparator(ch)) {
                if (ch == _T('.')) {
                    token = TOKEN_BEGIN_STRING_ID;
                } else {
                    // TOKEN_ERROR
                    error_substr = translations.substr(0, pos + 1);
                    return;
                }
            }
            break;

        case TOKEN_BEGIN_VALUE: {
            size_t end = translations.find_first_of(_T('\n'), pos);
            tstring val;
            if (end == tstring::npos) {
                val = translations.substr(pos);
                incr = len - pos;
            } else {
                val = translations.substr(pos, end - pos);
                incr = end - pos;
            }

            if (!val.empty() && val.back() == _T('\r'))
                val.pop_back();

            size_t p = val.find(_T("\\n"));
            while (p != std::string::npos) {
                val.replace(p, 2, _T("\\"));
                val[p] = _T('\n');
                p = val.find(_T("\\n"), p + 1);
            }
            if (!currentLocale.empty() && translMap.find(stringId) != translMap.end())
                translMap[stringId][currentLocale] = val;

            token = TOKEN_END_VALUE;
            break;
        }

        default:
            break;
        }
        pos += incr;
        if (pos == len)
            token = TOKEN_END_DOCUMENT;
    }

    if (token == TOKEN_END_DOCUMENT)
        is_translations_valid = true;
}
