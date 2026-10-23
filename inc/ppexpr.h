//
// ppexpr.h: constant expressions for the v32as preprocessor (%if, %assign, %eval)
//
// Values are what the Vircon32 CPU holds: 32-bit signed integers (arithmetic wraps) and
// 32-bit floats. An operation with a float operand is a float operation.
//
//   literals    42  0x2A  0b101010  052  2Ah  #42  'A'  '\n'  3.25  true  false
//   operators   ( )  + - ! ~ (unary)   * / %   + -   << >>   < <= > >=   == !=   &   ^   |
//               &&   ||   ?:        (C precedence; && || ?: do not evaluate what they skip)
//   functions   int(x) float(x) abs(x) min(a,b) max(a,b) floor(x) ceil(x) round(x)
//   names       the port, port-value and constant names the assembler itself knows
//               (e.g. GPU_Command); anything else is an error
//
////////////////////////////////////////////////////////////////////////////////////////

#ifndef V32AS_PPEXPR_H
#define V32AS_PPEXPR_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    int      is_float;
    int32_t  i;
    float    f;
}
ppval_t;

// Evaluates s[0..n). Returns 0, or -1 with a message in err.
int   ppexpr_eval     (const char *s, size_t n, ppval_t *out, char *err, size_t errsize);

// The value as assembly source text that scans back to the same value
// (integers in decimal, floats as d+.d+ with enough digits to round-trip)
void  ppexpr_format   (const ppval_t *v, char *buf, size_t size);

// Truth value: non-zero
int   ppexpr_truth    (const ppval_t *v);

#endif
