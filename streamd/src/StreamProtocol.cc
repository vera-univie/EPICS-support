/*************************************************************************
* This is the protocol parser of StreamDevice.
* Please see ../docs/ for detailed documentation.
*
* (C) 1999,2005 Dirk Zimoch (dirk.zimoch@psi.ch)
*
* This file is part of StreamDevice.
*
* StreamDevice is free software: You can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published
* by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* StreamDevice is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with StreamDevice. If not, see https://www.gnu.org/licenses/.
*************************************************************************/

#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>

#include "StreamProtocol.h"
#include "StreamFormatConverter.h"
#include "StreamError.h"

const char* StreamFormatTypeStr[] = {
    // must match the order in StreamFormat.h
    "none", "unsigned", "signed", "enum", "double", "string", "pseudo"
};

class StreamProtocolParser::Protocol::Variable
{
    friend class Protocol;
    friend class StreamProtocolParser;

    Variable* next;
    const StreamBuffer name;
    StreamBuffer value;
    int line;
    bool used;

    Variable(const char* name, int line, size_t startsize=0);
    Variable(const Variable& v);
    ~Variable();
};

//////////////////////////////////////////////////////////////////////////////
// StreamProtocolParser

StreamProtocolParser* StreamProtocolParser::parsers = NULL;
const char* StreamProtocolParser::path = NULL;
static const char* specialChars = " ,;{}=()$'\"+-*/";

// Client destructor
StreamProtocolParser::Client::
~Client()
{
}

// Private constructor
StreamProtocolParser::
StreamProtocolParser(FILE* file, const char* filename)
    : filename(filename), file(file), globalSettings(filename)
{

    next = parsers;
    parsers = this;
    // start parsing in global context
    protocols = NULL;
    line = 1;
    quote = false;
    valid = parseProtocol(globalSettings, globalSettings.commands);
}

// Private destructor
StreamProtocolParser::
~StreamProtocolParser()
{
    delete protocols;
    delete next;
}

void StreamProtocolParser::
report()
{
    Protocol* p;
    printf("Report of protocol file '%s'\n", filename());
    printf(" GLOBAL:\n");
    globalSettings.report();
    printf(" PROTOCOLS:\n");
    for (p = protocols; p; p = p->next)
    {
        p->report();
    }
}

// API function: read protocol from file, create parser if necessary
// RETURNS: a copy of a protocol that must be deleted by the caller
// SIDEEFFECTS: file IO, memory allocation for parsers
StreamProtocolParser::Protocol* StreamProtocolParser::
getProtocol(const char* filename, const StreamBuffer& protocolAndParams)
{
    StreamProtocolParser* parser;

    // Have we already seen this file?
    for (parser = parsers; parser; parser = parser->next)
    {
        if (parser->filename.startswith(filename))
        {
            if (!parser->valid)
            {
                error("Protocol file '%s' is invalid (see above)\n",
                    filename);
                return NULL;
            }
            break;
        }
    }
    if (!parser)
    {
        // If not, read it.
        parser = readFile(filename);
        if (!parser)
        {
            // We could not read the file.
            return NULL;
        }
    }
    return parser->getProtocol(protocolAndParams);
}

// API function: free all parser resources allocated by any getProtocol()
// Call this function after the last getProtocol() to clean up.
void StreamProtocolParser::
free()
{
    delete parsers;
    parsers = NULL;
}

/*
STEP 1: Parse the protocol file

Read the complete file, check structure and break it into protocols,
handlers and assignments.
Substitute protocol references and variable references.
No replacement of positional parameters ($1...$9).
No coding of parameters, commands, functions, or formats yet. We'll do
this after protocol arguments have been replaced.
*/

StreamProtocolParser* StreamProtocolParser::
readFile(const char* filename)
{
    FILE* file = NULL;
    StreamProtocolParser* parser;
    const char *p;
    size_t n;
    StreamBuffer dir;

    // no path or absolute file name
    if (!path || filename[0] == '/'
#ifdef _WIN32
        || filename[0] == '\\' || (isalpha(filename[0]) && filename[1] == ':')
#endif
        ) {
        // absolute file name
        file = fopen(filename, "r");
        if (file) {
            debug("StreamProtocolParser::readFile: found '%s'\n", filename);
        } else {
            error("Can't find readable file '%s'\n", filename);
            return NULL;
        }
    } else {
        // look for filename in every dir in search path
        for (p = path; *p; p += n)
        {
            dir.clear();
            // allow ':' or ';' for OS independence
            // we need to be careful with drive letters though
            n = strcspn(p, ":;");
#ifdef _WIN32
            if (n == 1 && p[1] == ':' && isalpha(p[0]))
            {
                // driver letter
                n = 2 + strcspn(p+2, ":;");
            }
#endif
            dir.append(p, n);
            // append / after everything except empty path [or drive letter]
            // Windows is fine with / as well
            if (n) {
#ifdef _WIN32
                if (n != 2 || p[1] != ':' || !isalpha(p[0]))
#endif
                dir.append('/');
            }
            if (p[n]) n++; // skip the path separator
            dir.append(filename);
            // try to read the file
            debug("StreamProtocolParser::readFile: try '%s'\n", dir());
            file = fopen(dir(), "r");
            if (file) {
                debug("StreamProtocolParser::readFile: found '%s'\n", dir());
                break;
            }
        }
        if (!file) {
            error("Can't find readable file '%s' in '%s'\n", filename, path);
            return NULL;
        }
    }
    // file found; create a parser to read it
    parser = new StreamProtocolParser(file, filename);
    fclose(file);
    if (!parser->valid) return NULL;
    return parser;
}

/*
STEP 2: Compile protocols to executable format

Make copy of one of the parsed protocols.
Replace positional parameters.
Ask client how to compile formats and commands.
*/

// Get a copy of a protocol
// Substitutions in protocolname replace positional parameters
StreamProtocolParser::Protocol* StreamProtocolParser::
getProtocol(const StreamBuffer& protocolAndParams)
{
    StreamBuffer name = protocolAndParams;
    // make name case insensitive
    char* p;
    for (p = name(); *p; p++) *p = tolower(*p);
    // find and make a copy with parameters inserted
    Protocol* protocol;
    for (protocol = protocols; protocol; protocol = protocol->next)
    {
        if (protocol->protocolname.startswith(name()))
        // constructor also replaces parameters
        return new Protocol(*protocol, name, 0);
    }
    error("Protocol '%s' not found in protocol file '%s'\n",
        protocolAndParams(), filename());
    return NULL;
}

inline bool StreamProtocolParser::
isGlobalContext(const StreamBuffer* commands)
{
    return commands == globalSettings.commands;
}

inline bool StreamProtocolParser::
isHandlerContext(Protocol& protocol, const StreamBuffer* commands)
{
    return commands != protocol.commands;
}

bool StreamProtocolParser::
parseProtocol(Protocol& protocol, StreamBuffer* commands)
{
    StreamBuffer token;
    int op;
    int startline;

    commands->clear();
    while (true) // assignment and protocol parsing loop
    {
        // expect command name, protoclol name,
        // handler name, or variable name
        token.clear();
        startline = line;
        if (!readToken(token, " ,;{}=()$'\"", isGlobalContext(commands)))
            return false;
        debug2("StreamProtocolParser::parseProtocol:"
            " token='%s'\n", token.expand()());

        if (token[0] == 0)
        {
            // terminate at EOF in global context
            return true;
        }
        if (strchr(" ;", token[0]))
        {
            // skip whitespaces, comments and ';'
            // this must come after (token[0] == 0) check
            continue;
        }
        if (token[0] == '}')
        {
            if (!isGlobalContext(commands))
            {
                // end of protocol or handler definition
                return true;
            }
            error(line, filename(), "Unexpected '}' (no matching '{') in global context\n");
            return false;
        }
        if (token[0] == '{')
        {
            error(line, filename(), "Expect %s name before '%c'\n",
                isGlobalContext(commands) ? "protocol" : "handler",
                token[0]);
            return false;
        }
        if (token[0] == '=')
        {
            error(line, filename(), "Expect variable name before '%c'\n", token[0]);
            return false;
        }
        if (token[0] != '@' && !isalpha(token[0]))
        {
            error(line, filename(), "Unexpected '%s'\n", token());
            return false;
        }
        do op = readChar(); while (op == ' '); // what comes after token?
        if (op == '=')
        {
            // variable assignment
            if (isHandlerContext(protocol, commands))
            {
                error(line, filename(), "Variables are not allowed in handlers: %s\n",
                    token());
                return false;
            }
            if (token[0] == '@' || (token[0] >= '0' && token[0] <= '9'))
            {
                error(line, filename(), "Variable name cannot start with '%c': %s\n",
                    token[0], token());
                return false;
            }
            if (!parseAssignment(token(), protocol))
            {
                line = startline;
                error(line, filename(), "in variable assignment '%s = ...'\n", token());
                return false;
            }
            continue;
        }
        if (op == '{') // protocol or handler definition
        {
            token.truncate(-1-(int)sizeof(int));
            if (token[0] == '@') // handler
            {
                if (isHandlerContext(protocol, commands))
                {
                    error(line, filename(), "Handlers are not allowed in handlers: %s\n",
                        token());
                    return false;
                }
                StreamBuffer* handler = protocol.createVariable(token(), line);
                handler->clear(); // override existing handler
                if (!parseProtocol(protocol, handler))
                {
                    line = startline;
                    error(line, filename(), "in handler '%s'\n", token());
                    return false;
                }
                continue;
            }
            // protocol definition
            if (!isGlobalContext(commands))
            {
                error(line, filename(), "Definition of '%s' not in global context (missing '}' ?)\n",
                    token());
                return false;
            }
            Protocol** ppP;
            for (ppP = &protocols; *ppP; ppP = &(*ppP)->next)
            {
                if ((*ppP)->protocolname.startswith(token()))
                {
                    error(line, filename(), "Protocol '%s' redefined\n", token());
                    return false;
                }
            }
            Protocol* pP = new Protocol(protocol, token, startline);
            if (!parseProtocol(*pP, pP->commands))
            {
                line = startline;
                error(line, filename(), "in protocol '%s'\n", token());
                delete pP;
                return false;
            }
            // append new protocol to parser
            *ppP = pP;
            continue;
        }
        if (token[0] == '@')
        {
            error(line, filename(), "Expect '{' after handler '%s'\n", token());
            return false;
        }
        // Must be a command or a protocol reference.
        if (isGlobalContext(commands))
        {
            error(line, filename(), "Expect '=' or '{' instead after '%s'\n",
                token());
            return false;
        }
        if (op == ';' || op == '}') // no arguments
        {
            // Check for protocol reference
            Protocol* p;
            for (p = protocols; p; p = p->next)
            {
                if (p->protocolname.startswith(token()))
                {
                    commands->append(*p->commands);
                    if (op == '}') ungetc(op, file);
                    break;
                }
            }
            if (p) continue;
            // Fall through for commands without arguments
        }
        // must be a command (validity will be checked later)
        commands->append(token); // is null separated
        ungetc(op, file); // put back first char after command
        if (parseValue(*commands, true) == false)
        {
            line = startline;
            error(line, filename(), "after command '%s'\n", token());
            return false;
        }
        debug2("parseProtocol: command '%s'\n", (*commands).expand()());
        commands->append('\0'); // terminate value with null byte
    }
}

int StreamProtocolParser::
readChar()
{
    int c;
    c = getc(file);
    if (isspace(c) || c == '#') // blanks or comments
    {
        do {
            if (c == '#') // comments
            {
                while ((c = getc(file)) != EOF && c != '\n');
            }
            if (c == '\n')
            {
                ++line; // count newlines
            }
            c = getc(file);
        } while (isspace(c) || c == '#');
        if (c != EOF) ungetc(c, file); // put back non-blank
        c = ' '; // return one space for all spaces and comments
    }
    return c;
}

bool StreamProtocolParser::
readToken(StreamBuffer& buffer, const char* specialchars, bool eofAllowed)
{
/*
a token is
    - a string in "" or '' (possibly with escaped \" or \' inside)
    - one of specialchars
    - EOF
    - a variable reference starting with $ (quoted or unquoted)
    - a word of chars not containing any of the above mapped to lowercase

All comments starting with # ending at end of line are whitespaces.
Sequences of whitespaces are be compressed into one blank.
EOF translates to a null byte.
Token is copied from file to buffer, variable names, words and strings are
terminated and the line number is stored for later use.
Each time newline is read, line is incremented.
*/
    if (!specialchars) specialchars = specialChars;
    size_t token = buffer.length();
    int l = line;

    int c = readChar();
    if (c == '$')
    {
        // a variable
        debug2("StreamProtocolParser::readToken: Variable\n");
        buffer.append(c);
        if (quote) buffer.append('"'); // mark as quoted variable
        c = getc(file);
        if (c >= '0' && c <= '9')
        {
            // positional parameter $0 ... $9
            buffer.append(c);
            buffer.append('\0').append(&l, sizeof(l)); // append line number
            return true;
        }
        if (c == '{')
        {
            int q = quote;
            quote = false;
            // variable like ${xyz}
            if (!readToken(buffer, "{}=;")) return false;
            debug2("StreamProtocolParser::readToken: Variable '%s' in {}\n",
                buffer(token));
            c = getc(file);
            if (c != '}')
            {
                error(line, filename(), "Expect '}' instead of '%c' after: %s\n",
                        c, buffer(token));
                return false;
            }
            quote = q;
            return true;
        }
        if (c == EOF)
        {
            error(line, filename(), "Unexpected end of file after '$' (looking for '}')\n");
            return false;
        }
        if (strchr (specialchars, c))
        {
            error(line, filename(), "Unexpected '%c' after '$'\n,", c);
            return false;
        }
        // variable like $xyz handled as word token
    }
    else if (quote || c == '\'' || c == '"')
    {
        debug2("StreamProtocolParser::readToken: Quoted string\n");
        // quoted string
        if (!quote)
        {
            quote = c;
            c = getc(file);
        }
        buffer.append(quote);
        while (quote)
        {
            if (c == EOF || c == '\n')
            {
                error(line, filename(), "Unterminated quoted string: %s\n",
                    buffer(token));
                return false;
            }
            buffer.append(c);
            if (c == quote)
            {
                quote = false;
                break;
            }
            if (c == '\\')
            {
                c = getc(file);
                if (c == '$')
                {
                    // quoted variable reference
                    // terminate string here and do variable in next pass
                    buffer[-1] = quote;
                    ungetc(c, file);
                    break;
                }
                if (c == EOF || c == '\n')
                {
                    error(line, filename(), "Backslash at end of line: %s\n",
                        buffer(token));
                    return false;
                }
                buffer.append(c);
            }
            c = getc(file);
        }
        buffer.append('\0').append(&l, sizeof(l)); // append line number
        return true;
    }
    else if (c == EOF)
    {
        // end of file
        if (!eofAllowed)
        {
            error(line, filename(), "Unexpected end of file (looking for '}')\n");
            return false;
        }
        buffer.append('\0');
        return true;
    }
    else if (strchr (specialchars, c))
    {
        debug2("StreamProtocolParser::readToken: Special '%c'\n", c);
        // a single character
        buffer.append(c);
        return true;
    }
    // a word or (variable name)
    debug2("StreamProtocolParser::readToken: word\n");
    while (1)
    {
        buffer.append(tolower(c));
        if ((c = readChar()) == EOF) break;
        if (strchr (specialchars, c))
        {
            ungetc(c, file); // put back char of next token
            break;
        }
    }
    debug2("StreamProtocolParser::readToken: word='%s' c='%c'\n",
        buffer(token), c);
    buffer.append('\0').append(&l, sizeof(l)); // append line number
    return true;
}

bool StreamProtocolParser::
parseAssignment(const char* name, Protocol& protocol)
{
    StreamBuffer value;

    if (!parseValue (value)) return false;
    *protocol.createVariable(name, line) = value;  // transfer value
    return true;
}

bool StreamProtocolParser::
parseValue(StreamBuffer& buffer, bool lazy)
{
    size_t token;
    int c;

    do c = readChar(); while (c == ' '); // skip leading spaces
    ungetc (c, file);
    while (true)
    {
        token = buffer.length(); // start of next token
        if (!readToken(buffer)) return false;
        debug2("StreamProtocolParser::parseValue:%d: %s\n",
            line, buffer.expand(token)());
        c = buffer[token];
        if (c == '$') // a variable
        {
            size_t varname = token+1;
            if (buffer[varname] == '"') varname++; // quoted variable
            if (lazy || (buffer[varname] >= '0' && buffer[varname] <= '9'))
            {
                // replace later
                continue;
            }
            StreamBuffer value;
            if (!globalSettings.replaceVariable(value, buffer(token)))
                return false;
            buffer.replace(token, buffer.length(), value);
            continue;
        }
        if (c == '{' || c == '=')
        {
            error(line, filename(), "Unexpected '%c' (missing ';' or '\"' ?)\n", c);
            return false;
        }
        if (strchr (";}", c))
        {
            buffer.truncate(-1);
            if (c != ';')
            {
                // let's be generous with missing ';' before '}'
                ungetc (c, file);
            }
            return true;
        }
    }
}

// tools (static member functions)

const char* StreamProtocolParser::
printString(StreamBuffer& buffer, const char* s)
{
    while (*s)
    {
        switch (*s)
        {
            case esc:
                buffer.print("\\x%02x", (*++s) & 0xff);
                break;
            case '\r':
                buffer.append("\\r");
                break;
            case '\n':
                buffer.append("\\n");
                break;
            case skip:
                buffer.append("\\?");
                break;
            case whitespace:
                buffer.append("\\_");
                break;
            case '"':
                buffer.append("\\\"");
                break;
            case '\\':
                buffer.append("\\\\");
                break;
            case format_field:
                // <format_field> field <eos> addrLength AddressStructure formatstr <eos> StreamFormat [info <eos>]
                unsigned short fieldSize;
                buffer.print("%%(%s)", ++s);
                while (*s++);
                fieldSize = extract<unsigned short>(s);
                s += fieldSize; // skip fieldAddress
                goto format;
            case format:
                // <format> formatstr <eos> StreamFormat [info <eos>]
                buffer.append('%');
                s++;
format:         {
                    s = printString(buffer, s);
                    const StreamFormat& f = extract<StreamFormat>(s);
                    s += f.infolen;
                }
                continue;
            default:
                if ((*s & 0x7f) < 0x20 || (*s & 0x7f) == 0x7f)
                    buffer.print("\\x%02x", *s & 0xff);
                else
                    buffer.append(*s);
        }
        ++s;
    }
    return ++s;
}

//////////////////////////////////////////////////////////////////////////////
// StreamProtocolParser::Protocol::Variable

StreamProtocolParser::Protocol::Variable::
Variable(const char* name, int line, size_t startsize)
    : name(name), value(startsize), line(line)
{
    next = NULL;
    used = false;
}

StreamProtocolParser::Protocol::Variable::
Variable(const Variable& v)
    : name(v.name), value(v.value)
{
    line = v.line;
    used = v.used;
    next = NULL;
}

StreamProtocolParser::Protocol::Variable::
~Variable()
{
    delete next;
}

//////////////////////////////////////////////////////////////////////////////
// StreamProtocolParser::Protocol

// for global settings
StreamProtocolParser::Protocol::
Protocol(const char* filename)
    : filename(filename)
{
    line = 0;
    next = NULL;
    variables = new Variable(NULL, 0, 500);
    commands = &variables->value;
}

// make a deep copy
StreamProtocolParser::Protocol::
Protocol(const Protocol& p, StreamBuffer& name, int _line)
    : protocolname(name), filename(p.filename)
{
    next = NULL;
    // copy all variables
    Variable* pV;
    Variable** ppNewV = &variables;
    line = _line ? _line : p.line;
    debug("new Protocol(name=\"%s\", line=%d)\n", name(), line);
    for (pV = p.variables; pV; pV = pV->next)
    {
        *ppNewV = new Variable(*pV);
        ppNewV = &(*ppNewV)->next;
    }
    commands = &variables->value;
    if (line) variables->line = line;
    // get parameters from name
    memset(parameter, 0, sizeof(parameter));
    int i;
    const char* nextparameter;
    parameter[0] = protocolname();
    for (i = 0; i < 9; i++)
    {
        if (i) debug("StreamProtocolParser::Protocol::Protocol $%d=\"%s\"\n",
            i, parameter[i]);
        nextparameter = parameter[i] + strlen(parameter[i]) + 1;
        if (nextparameter > name.length() + parameter[0]) break;
        parameter[i+1] = nextparameter;
    }
}

StreamProtocolParser::Protocol::
~Protocol()
{
    delete variables;
    delete next;
}

void StreamProtocolParser::Protocol::
report()
{
    Variable* pV;
    if (protocolname) printf("  Protocol %s\n", protocolname.expand()());
    printf("    Variables:\n");
    for (pV = variables->next; pV; pV = pV->next) // 1st var is commands
    {
        if (pV->name[0] != '@')
        {
            printf("    %s = %s;\n", pV->name.expand()(), pV->value.expand()());
        }
    }
    printf("    Handlers:\n");
    for (pV = variables->next; pV; pV = pV->next)
    {
        if (pV->name[0] == '@')
        {
            printf("    %s {%s}\n", pV->name.expand()(), pV->value.expand()());
        }
    }
    printf("    Commands:\n");
    printf("     { %s }\n", commands->expand()());
}

StreamBuffer* StreamProtocolParser::Protocol::
createVariable(const char* name, int linenr)
{
    Variable** ppV;
    for (ppV = &variables; *ppV; ppV = &(*ppV)->next)
    {
        if ((*ppV)->name.startswith(name))
        {
            (*ppV)->line = linenr;
            return &(*ppV)->value;
        }
    }
    *ppV = new Variable(name, linenr);
    return &(*ppV)->value;
}

const StreamProtocolParser::Protocol::Variable*
    StreamProtocolParser::Protocol::
getVariable(const char* name)
{
    Variable* pV;

    for (pV = variables; pV; pV = pV->next)
    {
        if (pV->name.startswith(name))
        {
            pV->used = true;
            return pV;
        }
    }
    return NULL;
}

bool StreamProtocolParser::Protocol::
getNumberVariable(const char* varname, unsigned long& value, unsigned long max)
{
    const Variable* pvar = getVariable(varname);
    if (!pvar) return true;
    const char* source = pvar->value();
    if (!compileNumber(value, source, max))
    {
        int linenr = getLineNumber(source);
        error(linenr, filename(), "in variable %s\n", varname);
        return false;
    }
    if (source != pvar->value.end())
    {
        error(getLineNumber(source), filename(),
            "Garbage in variable '%s' after numeric value %ld: %s\n",
                varname, value, source);
        return false;
    }
    return true;
}

bool StreamProtocolParser::Protocol::
getEnumVariable(const char* varname, unsigned short& value, const char** enumstrings)
{
    const Variable* pvar = getVariable(varname);
    if (!pvar) return true;
    for (value = 0; enumstrings[value]; value++)
    {
        if (pvar->value.startswith(enumstrings[value]))
        {
            return true;
        }
    }
    error("Value '%s' must be one of", pvar->value());
    for (value = 0; enumstrings[value]; value++)
    {
        error("%s '%s'", value ? " or" : "", enumstrings[value]);
    }
    error("\n"
          "in variable '%s' in protocol file '%s' line %d\n",
          varname, filename(), getLineNumber(pvar->value()));
    return false;
}

bool StreamProtocolParser::Protocol::
getStringVariable(const char* varname, StreamBuffer& value, bool* defined)
{
    value.clear();
    const Variable* pvar = getVariable(varname);
    if (!pvar) return true;
    if (defined) *defined = true;
    const StreamBuffer* pvalue = &pvar->value;
    const char* source = (*pvalue)();
    if (!compileString(value, source))
    {
        error("in string variable '%s' in protocol file '%s' line %d\n",
                varname, filename(), getLineNumber(source));
        debug("%s = %s\n", varname, pvalue->expand()());
        return false;
    }
    if (source != pvalue->end())
    {
        debug("%s = %s\n", varname, pvalue->expand()());
        debug("  => %s\n", value.expand()());
        error("INTERNAL ERROR after '%s': source = %p != %p\n",
            varname, source, pvalue->end());
        return false;
    }
    return true;
}

bool StreamProtocolParser::Protocol::
getCommands(const char* handlername, StreamBuffer& code, Client* client)
{
    code.clear();
    const Variable* pvar = getVariable(handlername);
    if (!pvar) return true;
    if (!pvar->value) return true;
    const char* source = pvar->value();
    debug2("StreamProtocolParser::Protocol::getCommands"
        "(handlername=\"%s\", client=\"%s\"): source=%s\n",
            handlername, client->name(), pvar->value.expand()());
    if (!compileCommands(code, source, client))
    {
        if (handlername)
        {
            error(pvar->line, filename(),
                "in handler '%s'\n", handlername);
            error(variables->line, filename(),
                "used by protocol '%s'\n", protocolname());
            return false;
        }
        error(pvar->line, filename(),
            "in protocol '%s'\n", protocolname());
        return false;
    }
    debug2("commands %s: %s\n", handlername, pvar->value.expand()());
    debug2("compiled to: %s\n", code.expand()());
    return true;
}

bool StreamProtocolParser::Protocol::
replaceVariable(StreamBuffer& buffer, const char* varname)
{
    debug2("StreamProtocolParser::Protocol::replaceVariable %s\n", varname);
    bool quoted = false;
    if (*++varname == '"')
    {
        varname++;
        quoted = true;
    }
    int linenr = getLineNumber(varname);
    if (*varname >= '0' && *varname <= '9')
    {
        const char* p = parameter[*varname-'0'];
        if (!p)
        {
            error(linenr, filename(),
                "Missing value for parameter $%c\n", *varname);
            return false;
        }
        if (!quoted)
        {
            buffer.append(p).append('\0');
            buffer.append(&linenr, sizeof(linenr));
            return true;
        }
        buffer.append('"');
        bool escaped = false;
        while (*p)
        {
            if (*p == '"' && !escaped) buffer.append('\\');
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            buffer.append(*p++);
        }
        buffer.append('"').append('\0');
        buffer.append(&linenr, sizeof(linenr));
        return true;
    }
    const Variable* v = getVariable(varname);
    if (!v)
    {
        error(linenr, filename(),
            "Undefined variable '%s' referenced\n",
            varname);
        return false;
    }
    if (!quoted)
    {
        buffer.append(v->value);
        return true;
    }
    // quoted
    buffer.append('"');
    size_t i;
    bool escaped = false;
    for (i = 0; i < v->value.length(); i++)
    {
        char c = v->value[i];
        if (c == '"' && !escaped) buffer.append('\\');
        if (c == 0 && !escaped)
        {
            // skip line info of token
            i += sizeof(linenr);
            continue;
        }
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        buffer.append(c);
    }
    buffer.append('"').append('\0');
    linenr = v->line;
    buffer.append(&linenr, sizeof(linenr));
    return true;
}

bool StreamProtocolParser::Protocol::
compileNumber(unsigned long& number, const char*& source, unsigned long max)
{
    char* end;
    unsigned long n;
    StreamBuffer buffer;

    debug2("StreamProtocolParser::Protocol::compileNumber source=\"%s\"\n",
        source);
    while (*source == '$' || (*source >= '0' && *source <= '9'))
    {
        debug2("StreamProtocolParser::Protocol::compileNumber *source=%u source=\"%s\"\n",
            *source, source);
        if (*source == '$')
        {
            if (!replaceVariable(buffer, source)) return false;
            debug2("buffer=%s\n", buffer.expand()());
            buffer.truncate(-1-(int)sizeof(int));
        }
        else
        {
            buffer.append(source);
        }
        source += strlen(source) + 1 + sizeof(int); // skip eos + line
    };
    n = strtoul(buffer(), &end, 0);
    if (end == buffer())
    {
        debug("StreamProtocolParser::Protocol::compileNumber: %s\n",
            buffer.expand()());
        error(getLineNumber(source), filename(),
            "Unsigned numeric value expected: %s\n", buffer());
        return false;
    }
    if (*end)
    {
        debug("StreamProtocolParser::Protocol::compileNumber: %s\n",
            buffer.expand()());
        error(getLineNumber(source), filename(),
            "Garbage after numeric value: %s\n", buffer());
        return false;
    }
    if (n > max)
    {
        debug("StreamProtocolParser::Protocol::compileNumber: %s\n",
            buffer.expand()());
        error(getLineNumber(source), filename(),
            "Value %s out of range [0...%ld]\n", buffer(), max);
        return false;
    }
    number = n;
    debug2("StreamProtocolParser::Protocol::compileNumber %s = %ld\n",
        buffer(), n);
    return true;
}

// read a string (quoted or numerical byte values) from source code line
// and put it to buffer, replacing all variable references and code formats

bool StreamProtocolParser::Protocol::
compileString(StreamBuffer& buffer, const char*& source,
    FormatType formatType, Client* client, int quoted, int recursionDepth)
{
    bool escaped = false;
    int newline = 0;
    StreamBuffer formatbuffer;
    size_t formatpos = buffer.length();
    line = getLineNumber(source);

    debug2("StreamProtocolParser::Protocol::compileString "
        "line %d source=\"%s\" formatType=%s quoted=%i recursionDepth=%d\n",
        line, source, ::toStr(formatType), quoted, recursionDepth);

    // coding is done in two steps:
    // 1) read a line from protocol source and code quoted strings,
    //    numerical bytes, tokens, etc, and replace variables and parameters
    // 2) replace the formats in the line
    // thus variables can be replaced inside the format info string

    while (1)
    {
        // this is step 2: replacing the formats
        if (!*source || (newline = getLineNumber(source)) != line)
        {
            debug2("StreamProtocolParser::Protocol::compileString line %i: %s\n", line, buffer.expand()());
            // compile all formats in this line
            // We do this here after all variables in this line
            // have been replaced and after string has been coded.
            if (recursionDepth == 0 && formatType != NoFormat)
            {
                int nformats=0;
                char c;
                while (formatpos < buffer.length() && (c = buffer[formatpos]) != '\0')
                {
                    if (c == esc) {
                        // ignore escaped %
                        formatpos+=2;
                        continue;
                    }
                    if (c == '%') {
                        if (buffer[formatpos+1] == '%') {
                            // treat %% as literal % like printf/scanf do
                            // replace with escaped %
                            buffer[formatpos] = esc;
                            formatpos+=2;
                            continue;
                        }
                        debug2("StreamProtocolParser::Protocol::compileString "
                            "format=\"%s\"\n", buffer.expand(formatpos)());
                        nformats++;
                        formatbuffer.clear();
                        const char* p = buffer(formatpos);
                        if (!compileFormat(formatbuffer, p, formatType, client))
                        {
                            p = buffer(formatpos);
                            formatbuffer.clear();
                            printString(formatbuffer, p);
                            error(line, filename(),
                                "in format string: \"%s\"\n",
                                formatbuffer());
                            return false;
                        }
                        size_t formatlen = p - buffer(formatpos);
                        buffer.replace(formatpos, formatlen, formatbuffer);
                        debug2("StreamProtocolParser::Protocol::compileString "
                            "replaced by: \"%s\"\n", buffer.expand(formatpos)());
                        formatpos += formatbuffer.length();
                        continue;
                    }
                    formatpos ++;
                }
                debug2("StreamProtocolParser::Protocol::compileString "
                    "%d formats found in line %d\n", nformats, line);
            }
            if (!*source) break;
            line = newline;
        }
        // this is step 1: coding the string
        if ((*source & 0x7f) < 0x20)
        {
            error("Unexpected byte %#04x\n",  *source & 0xFF);
            return false;
        }
        if (escaped)   // char after \ in quoted text
        {
            escaped = false;
            unsigned int c;
            int n=1;
            switch (*source)
            {
                case '$': // can't be: readToken would have made a token from this
                    error(line, filename(),
                        "INTERNAL ERROR: unconverted \\$ in quoted string\n");
                    return false;
                case '?':
                    buffer.append(skip);
                    source++;
                    continue;
                case '_':
                    buffer.append(whitespace);
                    source++;
                    continue;
                case 'a':
                    c=7;
                    break;
                case 'b':
                    c=8;
                    break;
                case 't':
                    c=9;
                    break;
                case 'n':
                    c='\n';
                    break;
                case 'r':
                    c='\r';
                    break;
                case 'e':
                    c=esc;
                    break;
                case '0':  // octal numbers (max 4 digits)
                    sscanf (source, "%4o%n", &c, &n); //cannot fail
                    if (c > 0xFF)
                    {
                        error(line, filename(),
                            "Octal number %#o does not fit in byte: \"%s\"\n",
                            c, source-1);
                        return false;
                    }
                    break;
                case 'x':  // hex numbers (max 2 digits after x)
                    if (sscanf (source+1, "%2x%n", &c, &n) < 1)
                    {
                        error(line, filename(),
                            "Hex digit expected after \\x: \"%s\"\n",
                            source-1);
                        return false;
                    }
                    n++;
                    break;
                case '1': // decimal numbers (max 3 digits)
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    sscanf (source, "%3u%n", &c, &n); //cannot fail
                    if (c > 0xFF)
                    {
                        error(line, filename(),
                            "Decimal number %d does not fit in byte: \"%s\"\n",
                            c, source-1);
                        return false;
                    }
                    break;
                default:  // escaped literals
                    c=*source;
            }
            source+=n;
            if (formatType != NoFormat)
            {
                buffer.append(esc);
            }
            buffer.append(c);
            continue;
        }
        if (quoted) // look for ending quotes and escapes
        {
            switch (*source)
            {
                case '\\':  // escape next character
                    escaped = true;
                    break;
                case '"':
                case '\'':
                    if (*source == quoted) // ending quote
                    {
                        source += 1+sizeof(int); // skip line number
                        quoted = false;
                        break;
                    }
                default:
                    buffer.append(*source); // literal character
            }
            source++;
            continue;
        }
        switch (*source) // tokens other than quoted string
        {
            const char* p;
            case '$': // parameter or variable
            {
                StreamBuffer value;
                if (!replaceVariable(value, source)) return false;
                source += strlen(source) + 1 + sizeof(int);
                p = value();
                int saveline = line;
                if (!compileString(buffer, p, formatType, client, false, recursionDepth + 1))
                    return false;
                line = saveline;
                continue;
            }
            case '\'':     // starting single quotes
            case '"':      // starting double quotes
                quoted = *source++;
                continue;
//            case '}':
//            case ')':
//                buffer.append(eos);
            case ' ':      // skip whitespace
            case ',':      // treat comma as whitespace
                source++;
                continue;
        }
        // try numeric byte value
        char* p;
        int c = strtol(source, &p, 0);
        if (p != source)
        {
            if (*p != 0)
            {
                error(line, filename(),
                    "Garbage after numeric source: %s", source);
                return false;
            }
            if (c > 0xFF || c < -0x80)
            {
                error(line, filename(),
                    "Value %s does not fit in byte\n", source);
                return false;
            }
            if (formatType != NoFormat)
            {
                buffer.append(esc);
            }
            buffer.append(c);
            source = p+1+sizeof(int);
            continue;
        }
        // try constant token
        struct {const char* name; char code;} codes [] =
        {
            {"skip", skip},
            {"?",    skip},
            {"nul",  0x00},
            {"soh",  0x01},
            {"stx",  0x02},
            {"etx",  0x03},
            {"eot",  0x04},
            {"enq",  0x05},
            {"ack",  0x06},
            {"bel",  0x07},
            {"bs",   0x08},
            {"ht",   0x09},
            {"tab",  0x09},
            {"lf",   0x0A},
            {"nl",   0x0A},
            {"vt",   0x0B},
            {"ff",   0x0C},
            {"np",   0x0C},
            {"cr",   0x0D},
            {"so",   0x0E},
            {"si",   0x0F},
            {"dle",  0x10},
            {"dc1",  0x11},
            {"dc2",  0x12},
            {"dc3",  0x13},
            {"dc4",  0x14},
            {"nak",  0x15},
            {"syn",  0x16},
            {"etb",  0x17},
            {"can",  0x18},
            {"em",   0x19},
            {"sub",  0x1A},
            {"esc",  0x1B},
            {"fs",   0x1C},
            {"gs",   0x1D},
            {"rs",   0x1E},
            {"us",   0x1F},
            {"del",  0x7F}
        };
        size_t i;
        c=-1;
        for (i = 0; i < sizeof(codes)/sizeof(*codes); i++)
        {
            if (strcmp(source, codes[i].name) == 0)
            {
                c = codes[i].code;
                if (c == skip && formatType != ScanFormat)
                {
                    error(line, filename(),
                        "Use of '%s' only allowed in input formats\n",
                        source);
                    return false;
                }
                if (formatType != NoFormat &&
                    i > 2 /* do not escape skip */)
                {
                    buffer.append(esc);
                }
                buffer.append(c);
                source += strlen(source) + 1 + sizeof(int);
                break;
            }
        }
        if (c >= 0) continue;
        // source may contain a function name
        error(line, filename(),
            "Unexpected '%s' in string\n", source);
        return false;
    }
    debug2("StreamProtocolParser::Protocol::compileString buffer=%s\n", buffer.expand()());
    return true;
}

bool StreamProtocolParser::Protocol::
compileFormat(StreamBuffer& buffer, const char*& formatstr,
    FormatType formatType, Client* client)
{
/*
    formatstr := '%' ['(' field ')'] [flags] [width] ['.' prec] conv [extra]
    field := string
    flags := '-' | '+' | ' ' | '#' | '0' | '*'
    width := integer
    prec :=  integer
    conv := character
    extra := string
    only conv is required all other parts are optional.

    compiles to:
    <format> formatstr <eos> StreamFormat [info <eos>]
    or:
    <format_field> field <eos> addrLength AddressStructure formatstr <eos> StreamFormat [info <eos>]
*/
    const char* source = formatstr;
    StreamFormat streamFormat;
    size_t fieldname = 0;
    // look for fieldname
    if (source[1] == '(')
    {
        buffer.append(format_field);
        if (!client)
        {
            error(line, filename(),
                "Using fieldname is not possible in this context\n");
            return false;
        }
        const char* fieldnameEnd = strchr(source+=2, ')');
        if (!fieldnameEnd)
        {
            error(line, filename(),
                "Missing ')' after field name\n");
            return false;
        }
        // add fieldname for debug purpose
        fieldname=buffer.length();
        buffer.append(source, fieldnameEnd - source).append(eos);
        debug2("StreamProtocolParser::Protocol::compileFormat: fieldname='%s'\n",
            buffer(fieldname));
        StreamBuffer fieldAddress;
        if (!client->getFieldAddress(buffer(fieldname), fieldAddress))
        {
            error(line, filename(),
                "Field '%s' not found\n", buffer(fieldname));
            return false;
        }
        source = fieldnameEnd;
        unsigned short length = (unsigned short)fieldAddress.length();
        buffer.append(&length, sizeof(length));
        buffer.append(fieldAddress);
    }
    else
    {
        buffer.append(format);
    }
    const char* formatstart = source + 1;

    // parse format and get info string
    StreamBuffer infoString;
    int type = StreamFormatConverter::parseFormat(source,
        formatType, streamFormat, infoString);

    if (!type)
    {
        // parsing failed
        return false;
    }
    if (type < 1 && type > pseudo_format)
    {
        error(line, filename(),
            "Illegal format type %d returned from '%%%c' converter\n",
            type, streamFormat.conv);
        return false;
    }
    if (type == pseudo_format && fieldname)
    {
        error(line, filename(),
            "Fieldname not allowed with pseudo format: '%%(%s)%c'\n",
            buffer(fieldname), streamFormat.conv);
        return false;
    }
    if (fieldname && streamFormat.flags & skip_flag)
    {
        error(line, filename(),
            "Use of skip modifier '*' not allowed "
            "together with redirection\n");
        return false;
    }
    streamFormat.type = static_cast<StreamFormatType>(type);
    if (infoString && infoString[-1] != eos)
    {
        // terminate if necessary
        infoString.append(eos);
    }
    streamFormat.infolen = (unsigned short)infoString.length();
    // add formatstr for debug purpose
    buffer.append(formatstart, source-formatstart).append(eos);

    debug2("StreamProtocolParser::Protocol::compileFormat: formatstring=\"%s\"\n",
        StreamBuffer(formatstart, source-formatstart).expand()());

    // add streamFormat structure and info
    buffer.append(&streamFormat, sizeof(streamFormat));
    buffer.append(infoString);

    debug2("StreamProtocolParser::Protocol::compileFormat: format.type=%s, "
        "infolen=%ld infostring=\"%s\"\n",
        StreamFormatTypeStr[streamFormat.type],
        streamFormat.infolen, infoString.expand()());
    formatstr = source; // move pointer after parsed format
    return true;
}

bool StreamProtocolParser::Protocol::
compileCommands(StreamBuffer& buffer, const char*& source, Client* client)
{
    const char* command;
    const char* args;

    while (*source)
    {
        command = source;
        args = source + strlen(source) + 1 + sizeof(int);
        if (!client->compileCommand(this, buffer, command, args))
        {
            error(getLineNumber(source), filename(),
                "in command '%s'\n", command);
            return false;
        }
        if (*args)
        {
            error(getLineNumber(source), filename(),
                "Garbage after '%s' command: '%s'\n",
                command, args);
            return false;
        }
        source = args + 1;
    }
    buffer.append(eos);
    return true;
}

bool StreamProtocolParser::Protocol::
checkUnused()
{
    Variable* pV;

    for (pV = variables; pV; pV = pV->next)
    {
        if (!pV->used)
        {
            if (pV->name[0] == '@')
            {
                error("Unknown handler %s defined in protocol file '%s' line %d\n",
                    pV->name(), filename(), pV->line);
                return false;
            }
            debug("Unused variable %s in protocol file '%s' line %d\n",
                pV->name(), filename(), pV->line);
        }
    }
    return true;
}
