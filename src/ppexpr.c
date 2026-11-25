//
// ppexpr.c: constant expression evaluator for the v32as preprocessor. See ppexpr.h.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <limits.h>

#include "v32as.h"
#include "ppexpr.h"

typedef struct
{
    const char  *s;
    size_t       n;
    size_t       pos;
    char        *err;
    size_t       errsize;
    int          failed;
    int          depth;             // nesting of ( ) unary operators and ?:, bounded: the parser recurses
}
ev_t;

#define  EV_MAX_DEPTH  200

static ppval_t  ev_ternary (ev_t *e, int live);

static void  ev_fail (ev_t *e, const char *fmt, ...)
    __attribute__ ((format (V32_PRINTF, 2, 3)));

#include <stdarg.h>

static void  ev_fail (ev_t *e, const char *fmt, ...)
{
    va_list  ap;

    if (e -> failed)
    {
        return;
    }

    e -> failed = 1;

    va_start (ap, fmt);
    vsnprintf (e -> err, e -> errsize, fmt, ap);
    va_end (ap);
}

static ppval_t  mk_int (int32_t v)
{
    ppval_t  r = { 0, v, 0.0f };
    return (r);
}

static ppval_t  mk_float (ev_t *e, double v)
{
    ppval_t  r = { 1, 0, 0.0f };
    float    f = (float) v;

    if (isnan (f) || isinf (f))
    {
        ev_fail (e, "float result out of range");
        f = 0.0f;
    }

    r.f = f;
    return (r);
}

static float  as_float (const ppval_t *v)
{
    return (v -> is_float ? v -> f : (float) v -> i);
}

static void  ev_space (ev_t *e)
{
    while (e -> pos < e -> n && isspace ((unsigned char) e -> s[e -> pos]))
    {
        e -> pos++;
    }
}

// next operator text, if it is exactly op (and not the start of a longer operator)
static int  ev_accept (ev_t *e, const char *op)
{
    size_t  len = strlen (op);

    ev_space (e);

    if (e -> pos + len > e -> n || memcmp (e -> s + e -> pos, op, len) != 0)
    {
        return (0);
    }

    {
        char  next = (e -> pos + len < e -> n) ? e -> s[e -> pos + len] : 0;
        char  a    = op[0];

        // don't take '&' of '&&', '|' of '||', '<' of '<<' or '<=', '=' alone ...
        if ((len == 1) && ((a == '&' && next == '&') || (a == '|' && next == '|')
                        || (a == '<' && (next == '<' || next == '='))
                        || (a == '>' && (next == '>' || next == '='))
                        || (a == '!' && next == '=')))
        {
            return (0);
        }
    }

    e -> pos += len;
    return (1);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Literals
//
static int  char_value (ev_t *e, const char *s, size_t n, size_t *i)
{
    char  c = s[(*i)++];

    if (c != '\\')
    {
        return ((unsigned char) c);
    }

    if (*i >= n)
    {
        ev_fail (e, "malformed character literal");
        return (0);
    }

    c = s[(*i)++];

    switch (c)
    {
        case 'n':  return ('\n');
        case 'r':  return ('\r');
        case 't':  return ('\t');
        case 'x':
            if (*i + 1 < n && isxdigit ((unsigned char) s[*i]) && isxdigit ((unsigned char) s[*i + 1]))
            {
                char  hex[3] = { s[*i], s[*i + 1], 0 };

                *i += 2;
                return ((int) strtol (hex, NULL, 16));
            }
            ev_fail (e, "bad hexadecimal escape (expected 2 hex digits)");
            return (0);
        default:
            return ((unsigned char) c);
    }
}

static int  all_hex_then_h (const char *s, size_t n)
{
    size_t  i;

    if (n < 2 || (s[n - 1] != 'h' && s[n - 1] != 'H'))
    {
        return (0);
    }

    for (i = 0; i + 1 < n; i++)
    {
        if (!isxdigit ((unsigned char) s[i]))
        {
            return (0);
        }
    }

    return (1);
}

static ppval_t  parse_integer (ev_t *e, const char *digits, int base)
{
    unsigned long long  v;
    char               *end;

    errno = 0;
    v     = strtoull (digits, &end, base);

    if (errno != 0 || *end != 0 || v > 0xFFFFFFFFull)
    {
        ev_fail (e, "number out of range for a 32-bit value");
        return (mk_int (0));
    }

    return (mk_int ((int32_t) (uint32_t) v));
}

static ppval_t  ev_number (ev_t *e)
{
    size_t  start = e -> pos;
    size_t  end   = start;
    char    word[80];
    size_t  len;
    int     k;

    while (end < e -> n && (isalnum ((unsigned char) e -> s[end]) || e -> s[end] == '_' || e -> s[end] == '.'))
    {
        end++;
    }

    len = end - start;

    if (len >= sizeof (word))
    {
        ev_fail (e, "number is too long");
        return (mk_int (0));
    }

    memcpy (word, e -> s + start, len);
    word[len] = 0;
    e -> pos  = end;

    if (len > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X'))
    {
        for (k = 2; k < (int) len; k++)
        {
            if (!isxdigit ((unsigned char) word[k]))
            {
                ev_fail (e, "bad hexadecimal number '%s'", word);
                return (mk_int (0));
            }
        }
        return (parse_integer (e, word + 2, 16));
    }

    if (len > 2 && word[0] == '0' && (word[1] == 'b' || word[1] == 'B'))
    {
        for (k = 2; k < (int) len; k++)
        {
            if (word[k] != '0' && word[k] != '1')
            {
                ev_fail (e, "bad binary number '%s'", word);
                return (mk_int (0));
            }
        }
        return (parse_integer (e, word + 2, 2));
    }

    if (all_hex_then_h (word, len))
    {
        word[len - 1] = 0;
        return (parse_integer (e, word, 16));
    }

    if (strchr (word, '.') != NULL)
    {
        char   *fend;
        float   f;
        int     dots = 0;
        int     digits_before = 0;

        for (k = 0; k < (int) len; k++)
        {
            if (word[k] == '.')
            {
                dots++;
            }
            else if (!isdigit ((unsigned char) word[k]))
            {
                ev_fail (e, "bad number '%s'", word);
                return (mk_int (0));
            }
            else if (dots == 0)
            {
                digits_before = 1;
            }
        }

        if (dots != 1 || !digits_before || word[len - 1] == '.')
        {
            ev_fail (e, "bad floating point number '%s'", word);
            return (mk_int (0));
        }

        f = strtof (word, &fend);

        if (isinf (f) || *fend != 0)
        {
            ev_fail (e, "number out of range for a float");
            return (mk_int (0));
        }

        return (mk_float (e, f));
    }

    for (k = 0; k < (int) len; k++)
    {
        if (!isdigit ((unsigned char) word[k]))
        {
            ev_fail (e, "bad number '%s'", word);
            return (mk_int (0));
        }
    }

    if (len > 1 && word[0] == '0')
    {
        for (k = 1; k < (int) len; k++)
        {
            if (word[k] > '7')
            {
                ev_fail (e, "invalid octal number '%s'", word);
                return (mk_int (0));
            }
        }
        return (parse_integer (e, word + 1, 8));
    }

    return (parse_integer (e, word, 10));
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Operators
//
static int  is_zero (const ppval_t *v)
{
    return (v -> is_float ? (v -> f == 0.0f) : (v -> i == 0));
}

int  ppexpr_truth (const ppval_t *v)
{
    return (!is_zero (v));
}

static ppval_t  arith (ev_t *e, char op, ppval_t a, ppval_t b, int live)
{
    if (a.is_float || b.is_float)
    {
        float  x = as_float (&a);
        float  y = as_float (&b);

        switch (op)
        {
            case '+':  return (mk_float (e, (double) x + (double) y));
            case '-':  return (mk_float (e, (double) x - (double) y));
            case '*':  return (mk_float (e, (double) x * (double) y));
            case '/':
                if (y == 0.0f)
                {
                    if (live) { ev_fail (e, "division by zero"); }
                    return (mk_float (e, 0.0));
                }
                return (mk_float (e, (double) x / (double) y));
            default:
                if (y == 0.0f)
                {
                    if (live) { ev_fail (e, "division by zero"); }
                    return (mk_float (e, 0.0));
                }
                return (mk_float (e, fmod ((double) x, (double) y)));
        }
    }

    {
        uint32_t  x = (uint32_t) a.i;
        uint32_t  y = (uint32_t) b.i;

        switch (op)
        {
            case '+':  return (mk_int ((int32_t) (x + y)));
            case '-':  return (mk_int ((int32_t) (x - y)));
            case '*':  return (mk_int ((int32_t) (x * y)));
            default:
                if (b.i == 0)
                {
                    if (live) { ev_fail (e, "division by zero"); }
                    return (mk_int (0));
                }
                if (a.i == INT32_MIN && b.i == -1)
                {
                    return (mk_int (op == '/' ? INT32_MIN : 0));
                }
                return (mk_int (op == '/' ? a.i / b.i : a.i % b.i));
        }
    }
}

static int  need_ints (ev_t *e, const ppval_t *a, const ppval_t *b, const char *op)
{
    if (a -> is_float || (b != NULL && b -> is_float))
    {
        ev_fail (e, "operator '%s' needs integer operands", op);
        return (0);
    }

    return (1);
}

static ppval_t  ev_function (ev_t *e, const char *name, int live);

static ppval_t  ev_primary (ev_t *e, int live)
{
    ppval_t  v = mk_int (0);

    ev_space (e);

    if (e -> pos >= e -> n)
    {
        ev_fail (e, "unexpected end of expression");
        return (v);
    }

    {
        unsigned char  c = (unsigned char) e -> s[e -> pos];

        if (c == '(')
        {
            e -> pos++;
            v = ev_ternary (e, live);

            if (!ev_accept (e, ")"))
            {
                ev_fail (e, "missing ')' in expression");
            }
            return (v);
        }

        if (isdigit (c))
        {
            return (ev_number (e));
        }

        if (c == '#' && e -> pos + 1 < e -> n)           // #42 / #-3, as the scanner has it
        {
            size_t  save = e -> pos + 1;
            int     neg  = 0;

            if (e -> s[save] == '+' || e -> s[save] == '-')
            {
                neg = (e -> s[save] == '-');
                save++;
            }

            if (save < e -> n && isdigit ((unsigned char) e -> s[save]))
            {
                e -> pos = save;
                v        = ev_number (e);

                if (!v.is_float && neg)
                {
                    v.i = (int32_t) (0u - (uint32_t) v.i);
                }
                return (v);
            }
        }

        if (c == '\'')
        {
            size_t  i = e -> pos + 1;
            int     ch;

            if (i >= e -> n)
            {
                ev_fail (e, "malformed character literal");
                return (v);
            }

            ch = char_value (e, e -> s, e -> n, &i);

            if (i >= e -> n || e -> s[i] != '\'')
            {
                ev_fail (e, "malformed character literal");
                return (v);
            }

            e -> pos = i + 1;
            return (mk_int (ch));
        }

        if (isalpha (c) || c == '_')
        {
            char    name[64];
            size_t  start = e -> pos;
            size_t  end   = start;
            size_t  len;

            while (end < e -> n && (isalnum ((unsigned char) e -> s[end]) || e -> s[end] == '_'))
            {
                end++;
            }

            len = end - start;

            if (all_hex_then_h (e -> s + start, len) && isdigit (c))
            {
                return (ev_number (e));
            }

            if (len >= sizeof (name))
            {
                ev_fail (e, "name is too long");
                return (v);
            }

            memcpy (name, e -> s + start, len);
            name[len] = 0;
            e -> pos  = end;

            if (all_hex_then_h (name, len))             // FFh
            {
                e -> pos = start;
                return (ev_number (e));
            }

            ev_space (e);

            if (e -> pos < e -> n && e -> s[e -> pos] == '(')
            {
                return (ev_function (e, name, live));
            }

            {
                const kw_t  *k = kw_lookup (name, len);

                if (k != NULL && (k -> kind == KW_CONSTANT || k -> kind == KW_PORT || k -> kind == KW_PORTVALUE))
                {
                    return (mk_int ((int32_t) k -> value));
                }

                if (k != NULL)
                {
                    ev_fail (e, "'%s' is not a constant (registers and instructions cannot be used in an expression)", name);
                }
                else if (name[0] == '_')
                {
                    ev_fail (e, "label '%s' has no value yet (addresses are only known after assembly)", name);
                }
                else
                {
                    ev_fail (e, "unknown name '%s' in expression", name);
                }
            }

            return (v);
        }

        ev_fail (e, "unexpected character '%c' in expression", (c >= 32 && c < 127) ? c : '?');
    }

    return (v);
}

static ppval_t  ev_function (ev_t *e, const char *name, int live)
{
    ppval_t  a[2];
    int      n = 0;

    e -> pos++;                                          // the '('
    ev_space (e);

    if (e -> pos < e -> n && e -> s[e -> pos] == ')')
    {
        e -> pos++;
    }
    else
    {
        for (;;)
        {
            if (n < 2)
            {
                a[n] = ev_ternary (e, live);
            }
            else
            {
                (void) ev_ternary (e, live);
            }
            n++;

            if (ev_accept (e, ","))
            {
                continue;
            }

            if (!ev_accept (e, ")"))
            {
                ev_fail (e, "missing ')' after the arguments of %s()", name);
            }
            break;
        }
    }

    if (e -> failed)
    {
        return (mk_int (0));
    }

    if (strcasecmp (name, "min") == 0 || strcasecmp (name, "max") == 0)
    {
        int  want_max = (strcasecmp (name, "max") == 0);

        if (n != 2)
        {
            ev_fail (e, "%s() takes 2 arguments", name);
            return (mk_int (0));
        }

        if (a[0].is_float || a[1].is_float)
        {
            float  x = as_float (&a[0]);
            float  y = as_float (&a[1]);

            return (mk_float (e, want_max ? (x > y ? x : y) : (x < y ? x : y)));
        }

        return (mk_int (want_max ? (a[0].i > a[1].i ? a[0].i : a[1].i) : (a[0].i < a[1].i ? a[0].i : a[1].i)));
    }

    if (n != 1)
    {
        ev_fail (e, "%s() takes 1 argument", name);
        return (mk_int (0));
    }

    if (strcasecmp (name, "int") == 0)
    {
        float  f;

        if (!a[0].is_float)
        {
            return (a[0]);
        }

        f = truncf (a[0].f);

        if (f >= 2147483648.0f || f < -2147483648.0f)
        {
            ev_fail (e, "int(): value out of range for a 32-bit integer");
            return (mk_int (0));
        }

        return (mk_int ((int32_t) f));
    }

    if (strcasecmp (name, "float") == 0)
    {
        return (mk_float (e, as_float (&a[0])));
    }

    if (strcasecmp (name, "abs") == 0)
    {
        if (a[0].is_float)
        {
            return (mk_float (e, fabsf (a[0].f)));
        }
        return (mk_int (a[0].i < 0 ? (int32_t) (0u - (uint32_t) a[0].i) : a[0].i));
    }

    if (strcasecmp (name, "floor") == 0)  return (mk_float (e, floorf (as_float (&a[0]))));
    if (strcasecmp (name, "ceil")  == 0)  return (mk_float (e, ceilf  (as_float (&a[0]))));
    if (strcasecmp (name, "round") == 0)  return (mk_float (e, roundf (as_float (&a[0]))));

    ev_fail (e, "unknown function '%s' in expression", name);
    return (mk_int (0));
}

static ppval_t  ev_unary (ev_t *e, int live)
{
    ev_space (e);

    if (e -> pos < e -> n)
    {
        char  c = e -> s[e -> pos];

        if (c == '-' || c == '+' || c == '~' || (c == '!' && !(e -> pos + 1 < e -> n && e -> s[e -> pos + 1] == '=')))
        {
            ppval_t  v;

            e -> pos++;

            if (++e -> depth > EV_MAX_DEPTH)
            {
                ev_fail (e, "expression is nested too deeply");
                e -> depth--;
                return (mk_int (0));
            }

            v = ev_unary (e, live);
            e -> depth--;

            if (c == '-')
            {
                return (v.is_float ? mk_float (e, -(double) v.f) : mk_int ((int32_t) (0u - (uint32_t) v.i)));
            }
            if (c == '~')
            {
                if (!need_ints (e, &v, NULL, "~"))
                {
                    return (mk_int (0));
                }
                return (mk_int (~v.i));
            }
            if (c == '!')
            {
                return (mk_int (is_zero (&v) ? 1 : 0));
            }
            return (v);
        }
    }

    return (ev_primary (e, live));
}

static ppval_t  ev_mul (ev_t *e, int live)
{
    ppval_t  v = ev_unary (e, live);

    for (;;)
    {
        char  op;

        ev_space (e);
        op = (e -> pos < e -> n) ? e -> s[e -> pos] : 0;

        if (op != '*' && op != '/' && op != '%')
        {
            return (v);
        }

        e -> pos++;
        v = arith (e, op, v, ev_unary (e, live), live);
    }
}

static ppval_t  ev_add (ev_t *e, int live)
{
    ppval_t  v = ev_mul (e, live);

    for (;;)
    {
        char  op;

        ev_space (e);
        op = (e -> pos < e -> n) ? e -> s[e -> pos] : 0;

        if (op != '+' && op != '-')
        {
            return (v);
        }

        e -> pos++;
        v = arith (e, op, v, ev_mul (e, live), live);
    }
}

static ppval_t  ev_shift (ev_t *e, int live)
{
    ppval_t  v = ev_add (e, live);

    for (;;)
    {
        int  left = ev_accept (e, "<<");

        if (!left && !ev_accept (e, ">>"))
        {
            return (v);
        }

        {
            ppval_t  r = ev_add (e, live);

            if (!need_ints (e, &v, &r, left ? "<<" : ">>"))
            {
                return (mk_int (0));
            }

            if (r.i < 0 || r.i > 31)
            {
                if (live) { ev_fail (e, "shift count out of range (0 to 31)"); }
                return (mk_int (0));
            }

            v = mk_int (left ? (int32_t) ((uint32_t) v.i << r.i) : (v.i >> r.i));
        }
    }
}

static ppval_t  ev_rel (ev_t *e, int live)
{
    ppval_t  v = ev_shift (e, live);

    for (;;)
    {
        const char  *op = NULL;
        ppval_t      r;
        int          res;
        double       x;
        double       y;

        if (ev_accept (e, "<="))       op = "<=";
        else if (ev_accept (e, ">="))  op = ">=";
        else if (ev_accept (e, "<"))   op = "<";
        else if (ev_accept (e, ">"))   op = ">";
        else                           return (v);

        r = ev_shift (e, live);
        x = v.is_float ? (double) v.f : (double) v.i;
        y = r.is_float ? (double) r.f : (double) r.i;

        res = op[0] == '<' ? (op[1] ? x <= y : x < y) : (op[1] ? x >= y : x > y);
        v   = mk_int (res);
    }
}

static ppval_t  ev_eq (ev_t *e, int live)
{
    ppval_t  v = ev_rel (e, live);

    for (;;)
    {
        int      eq = ev_accept (e, "==");
        ppval_t  r;
        double   x;
        double   y;

        if (!eq && !ev_accept (e, "!="))
        {
            return (v);
        }

        r = ev_rel (e, live);
        x = v.is_float ? (double) v.f : (double) v.i;
        y = r.is_float ? (double) r.f : (double) r.i;
        v = mk_int (eq ? (x == y) : (x != y));
    }
}

#define  BITLEVEL(NAME, NEXT, OPSTR, EXPR)                                               \
static ppval_t  NAME (ev_t *e, int live)                                                 \
{                                                                                        \
    ppval_t  v = NEXT (e, live);                                                         \
    while (ev_accept (e, OPSTR))                                                         \
    {                                                                                    \
        ppval_t  r = NEXT (e, live);                                                     \
        if (!need_ints (e, &v, &r, OPSTR)) { return (mk_int (0)); }                      \
        v = mk_int (EXPR);                                                               \
    }                                                                                    \
    return (v);                                                                          \
}

BITLEVEL (ev_band, ev_eq,   "&", v.i & r.i)
BITLEVEL (ev_bxor, ev_band, "^", v.i ^ r.i)
BITLEVEL (ev_bor,  ev_bxor, "|", v.i | r.i)

static ppval_t  ev_land (ev_t *e, int live)
{
    ppval_t  v = ev_bor (e, live);

    while (ev_accept (e, "&&"))
    {
        int      lv = live && !is_zero (&v);
        ppval_t  r  = ev_bor (e, lv);

        v = mk_int (!is_zero (&v) && !is_zero (&r));
    }

    return (v);
}

static ppval_t  ev_lor (ev_t *e, int live)
{
    ppval_t  v = ev_land (e, live);

    while (ev_accept (e, "||"))
    {
        int      lv = live && is_zero (&v);
        ppval_t  r  = ev_land (e, lv);

        v = mk_int (!is_zero (&v) || !is_zero (&r));
    }

    return (v);
}

static ppval_t  ev_ternary_1 (ev_t *e, int live)
{
    ppval_t  c = ev_lor (e, live);

    if (ev_accept (e, "?"))
    {
        int      yes = !is_zero (&c);
        ppval_t  a   = ev_ternary (e, live && yes);
        ppval_t  b;

        if (!ev_accept (e, ":"))
        {
            ev_fail (e, "missing ':' in '?:' expression");
            return (mk_int (0));
        }

        b = ev_ternary (e, live && !yes);

        return (yes ? a : b);
    }

    return (c);
}

static ppval_t  ev_ternary (ev_t *e, int live)
{
    ppval_t  v;

    if (++e -> depth > EV_MAX_DEPTH)
    {
        ev_fail (e, "expression is nested too deeply");
        e -> depth--;
        return (mk_int (0));
    }

    v = ev_ternary_1 (e, live);
    e -> depth--;

    return (v);
}

////////////////////////////////////////////////////////////////////////////////////////
//
// Public interface
//
int  ppexpr_eval (const char *s, size_t n, ppval_t *out, char *err, size_t errsize)
{
    ev_t  e = { s, n, 0, err, errsize, 0, 0 };

    err[0] = 0;
    *out   = ev_ternary (&e, 1);

    if (!e.failed)
    {
        ev_space (&e);

        if (e.pos < e.n)
        {
            ev_fail (&e, "unexpected '%c' in expression", (e.s[e.pos] >= 32 && e.s[e.pos] < 127) ? e.s[e.pos] : '?');
        }
    }

    return (e.failed ? -1 : 0);
}

void  ppexpr_format (const ppval_t *v, char *buf, size_t size)
{
    int  prec;

    if (!v -> is_float)
    {
        snprintf (buf, size, "%d", v -> i);
        return;
    }

    // the shortest fixed-point text (digits.digits, the only float form the scanner has)
    // that reads back as exactly this float
    for (prec = 1; prec <= 60; prec++)
    {
        snprintf (buf, size, "%.*f", prec, (double) v -> f);

        if (strtof (buf, NULL) == v -> f)
        {
            return;
        }
    }
}
