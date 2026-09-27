# COB coordinate math

Integer and floating-point helpers whose results must match 3.1c bit for bit:
unit-script coordinate queries, fixed-point vector scaling, 3D lengths and the
float-keyed max-heap the computer player uses to rank metal spots by distance.

`direction` converts both signed inputs to double, takes atan2, multiplies by
the exact binary64 radians-to-heading constant 3.1c uses, rounds to the
nearest even integer and keeps the low 16 bits. COB GET selectors 12 and 14
use it.

`distance` handles finite signed-int coordinates: the absolute coordinates are
normalized by their maximum, squared and summed, square-rooted and rescaled,
then truncated toward zero to 64 bits with the low 32 bits kept. COB GET
selectors 13 and 15 use it. NaN, infinity, errno and floating-point exception
behaviour are outside this integer API.

Every step is rounded to binary64 with no fused multiply-add. The tests pin
endpoints and the cases where summation order or truncation changes the
result; that the host libm's atan2 and sqrt give 3.1c's results on every input
is assumed, not proven.
