#pragma once

#include <stdint.h>
#include <string.h>
#include <mbedtls/sha512.h>
#include <mbedtls/bignum.h>
#include <assert.h>

unsigned char getbit(const unsigned char *t,int i) {
  return ((t[i>>3])>>(i&7))&1;
}

void setbit(unsigned char *t, int i,bool b) {
  if(b)
    (t[i>>3])|=(1<<(i&7));
  else
    (t[i>>3])&=~(1<<(i&7));
}

/* Field elements */

#define  F25519_SIZE 32



/* Field elements are represented as little-endian byte strings. All
 * operations have timings which are independent of input data, so they
 * can be safely used for cryptography.
 *
 * Computation is performed on un-normalized elements. These are byte
 * strings which fall into the range 0 <= x < 2p. Use f25519_normalize()
 * to convert to a value 0 <= x < p.
 *
 * Elements received from the outside may greater even than 2p.
 * f25519_normalize() will correctly deal with these numbers too.
 */

void unpack22(unsigned int *to,const unsigned char *t_)
{
  unsigned int *t=(unsigned int*)t_;
  
  unsigned int c;
  c=(t[0]);
  to[0]=c&0x3fffff;
  c=(t[0]>>22)|(t[1]<<10);
  to[1]=c&0x3fffff;
  c=(t[1]>>12)|(t[2]<<20);
  to[2]=c&0x3fffff;
  c=(t[2]>>2);
  to[3]=c&0x3fffff;
  c=(t[2]>>24)|(t[3]<<8);
  to[4]=c&0x3fffff;
  c=(t[3]>>14)|(t[4]<<18);
  to[5]=c&0x3fffff;
  c=(t[4]>>4);
  to[6]=c&0x3fffff;
  c=(t[4]>>26)|(t[5]<<6);
  to[7]=c&0x3fffff;
  c=(t[5]>>16)|(t[6]<<16);
  to[8]=c&0x3fffff;
  c=(t[6]>>6);
  to[9]=c&0x3fffff;
  c=(t[6]>>28)|(t[7]<<4);
  to[10]=c&0x3fffff;
  c=(t[7]>>18);
  to[11]=c&0x3fff;
}

void pack22(const unsigned int *to,unsigned char *t_)
{
  unsigned int c;
  unsigned int *t=(unsigned int*)t_;

  c=(to[0])|(to[1]<<22);
  t[0]=c;
  c=(to[1]>>10)|(to[2]<<12);
  t[1]=c;
  c=(to[2]>>20)|(to[3]<<2)|(to[4]<<24);
  t[2]=c;
  c=(to[4]>>8)|(to[5]<<14);
  t[3]=c;
  c=(to[5]>>18)|(to[6]<<4)|(to[7]<<26);
  t[4]=c;
  c=(to[7]>>6)|(to[8]<<16);
  t[5]=c;
  c=(to[8]>>16)|(to[9]<<6)|(to[10]<<28);
  t[6]=c;
  c=(to[10]>>4)|(to[11]<<18);
  t[7]=c;
}

struct f25519_t {
  uint32_t t22[12];
  f25519_t(int o=0) {
    memset(t22,0,sizeof(t22));
    t22[0]=o;
  }

  void aff()const {}
  
  f25519_t(unsigned char *tt) {
    unpack22(t22,tt);
  }
  void set(const uint8_t *tt) {
    unpack22(t22,tt);
  }
  void set(int o) {
    memset(t22,0,sizeof(t22));
    t22[0]=o;
  }

  void writebin(unsigned char *tt) const {
    pack22(t22,tt);
  }
  
  unsigned char parity() const {
    return t22[0]&1;
  }

};


void f25519_mul__distinct(f25519_t &r, const f25519_t &a, const f25519_t &b)
{
  unsigned long long c = 0;

  for (int i = 0; i < 12; i++) {
    int j;
    
    c >>= 22;
    for (j = 0; j <= i; j++)
      c += ((uint64_t)a.t22[j]) * (b.t22[i - j]);

    for (; j < 12; j++)
      c += ((uint64_t)a.t22[j]) *	(b.t22[i + 12 - j]) * 9728;

    r.t22[i] = c&0x3fffff;
  }
  r.t22[11] &= 0x1fff;
  c = (c >> 13) * 19;

  for (int i = 0; i < 12; i++) {
    c += r.t22[i];
    r.t22[i] = c&0x3fffff;
    c >>= 22;
  }
}



/* Identity constants */

f25519_t f25519_zero(0),f25519_one(1); 

/* Copy two points */
static inline void f25519_copy(f25519_t &x, const f25519_t &a)
{
  x=a;
}

void f25519_normalize(f25519_t &x);


void f25519_load(f25519_t &x, uint32_t c)
{
  x.set(c);
}

/* Conditional copy. If condition == 0, then zero is copied to dst. If
 * condition == 1, then one is copied to dst. Any other value results in
 * undefined behaviour.
 */

void f25519_select(f25519_t &dst,
		   const f25519_t &zero, const f25519_t &one,
		   uint8_t condition)
{
  const uint32_t mask = -condition;
  int i;
  for (i = 0; i < 12; i++)
    dst.t22[i] = zero.t22[i] ^ (mask & (one.t22[i] ^ zero.t22[i]));
}

void f25519_normalize(f25519_t &x)
{
  f25519_t minusp;
  uint16_t c;
  int i;

  /* The number is now less than 2^255 + 18, and therefore less than
   * 2p. Try subtracting p, and conditionally load the subtracted
   * value if underflow did not occur.
   */
  c = 19;

  for (i = 0; i + 1 < 12; i++) {
    c += x.t22[i];
    minusp.t22[i] = c;
    c >>= 22;
  }

  c += x.t22[11] - 0x2000;
  minusp.t22[11] = c;

  
  /* Load x-p if no underflow */
  f25519_select(x, minusp, x, (c >> 15) & 1);
}

uint8_t f25519_eq(const f25519_t &x, const f25519_t &y)
{
  uint32_t sum = 0;

  for (int i = 0; i < 12; i++)
    sum |= x.t22[i] ^ y.t22[i];

  sum |= (sum >> 16);
  sum |= (sum >> 8);
  sum |= (sum >> 4);
  sum |= (sum >> 2);
  sum |= (sum >> 1);

  return (sum ^ 1) & 1;
}

void f25519_add(f25519_t &r, const f25519_t &a, const f25519_t &b)
{
  uint32_t c = 0;
  int i;

  /* Add */
  c=0;
  for (i = 0; i < 12; i++) {
    c >>= 22;
    c += (a.t22[i]) + (b.t22[i]);
    r.t22[i] = c&0x3fffff;
  }

  /* Reduce with 2^255 = 19 mod p */
  r.t22[11] &= 0x1fff;
  c = (c >> 13) * 19;

  for (i = 0; i < 12; i++) {
    c += r.t22[i];
    r.t22[i] = c  & 0x3fffff;
    c >>= 22;
  }
}

void f25519_sub(f25519_t &r, const f25519_t &a, const f25519_t &b)
{
  uint32_t c = 0;
  int i;


  /* Calculate a + 2p - b, to avoid underflow */
  c = 0x7fffda;
  for (i = 0; i < 11; i++) {
    c += ((uint32_t)a.t22[i]) - ((uint32_t)b.t22[i]);
    r.t22[i] = c & 0x3fffff;
    c >>= 22;
    c+=0x7ffffe;
  }

  c-=0x7ffffe;
  c+=0x3ffe;
  
  c += ((uint32_t)a.t22[11]) - ((uint32_t)b.t22[11]);
  r.t22[11] = c & 0x1fff;
  c = (c >> 13) * 19;

  for (i = 0; i < 12; i++) {
    c += r.t22[i];
    r.t22[i] = c & 0x3fffff;;
    c >>= 22;
  }
}

void f25519_neg(f25519_t &r, const f25519_t &a)
{
  uint32_t c = 0;
  int i;

  c = 0x7fffda;
  for (i = 0; i < 11; i++) {
    c += - ((uint32_t)a.t22[i]);
    r.t22[i] = c & 0x3fffff;
    c >>= 22;
    c+=0x7ffffe;
  }

  c-=0x7ffffe;
  c+=0x3ffe;
  
  c -= ((uint32_t)a.t22[11]);
  r.t22[11] = c & 0x1fff;
  c = (c >> 13) * 19;

  for (i = 0; i < 12; i++) {
    c += r.t22[i];
    r.t22[i] = c & 0x3fffff;
    c >>= 22;
  }

}

void f25519_mul_c(f25519_t &r, const f25519_t &a, uint32_t b)
{
  uint32_t c = 0;
  int i;

  for (i = 0; i < 12; i++) {
    c >>= 22;
    c += b * ((uint32_t)a.t22[i]);
    r.t22[i] = c  & 0x3fffff;
  }

  r.t22[11] &= 0x1fff;
  c >>= 13;
  c *= 19;

  for (i = 0; i < 12; i++) {
    c += r.t22[i];
    r.t22[i] = c  & 0x3fffff;
    c >>= 22;
  }
}

void f25519_mul(f25519_t &r, const f25519_t &a, const f25519_t &b)
{
  f25519_t tmp;
  
  f25519_mul__distinct(tmp, a, b);
  f25519_copy(r, tmp);
}



/* Take the reciprocal of a field point. The __distinct variant is used
 * when r is known to be in a different location to x.
 */

void f25519_inv__distinct(f25519_t &r, const f25519_t &x)
{
  f25519_t s;

  /* This is a prime field, so by Fermat's little theorem:
   *
   *     x^(p-1) = 1 mod p
   *
   * Therefore, raise to (p-2) = 2^255-21 to get a multiplicative
   * inverse.
   *
   * This is a 255-bit binary number with the digits:
   *
   *     11111111... 01011
   *
   * We compute the result by the usual binary chain, but
   * alternate between keeping the accumulator in r and s, so as
   * to avoid copying temporaries.
   */

  /* 1 1 */
  f25519_mul__distinct(s, x, x);
  f25519_mul__distinct(r, s, x);

  /* 1 x 248 */
  for (int i = 0; i < 248; i++) {
    f25519_mul__distinct(s, r, r);
    f25519_mul__distinct(r, s, x);
  }

  /* 0 */
  f25519_mul__distinct(s, r, r);

  /* 1 */
  f25519_mul__distinct(r, s, s);
  f25519_mul__distinct(s, r, x);

  /* 0 */
  f25519_mul__distinct(r, s, s);

  /* 1 */
  f25519_mul__distinct(s, r, r);
  f25519_mul__distinct(r, s, x);

  /* 1 */
  f25519_mul__distinct(s, r, r);
  f25519_mul__distinct(r, s, x);
}

void f25519_inv(f25519_t &r, const f25519_t &x)
{
  f25519_t tmp;

  f25519_inv__distinct(tmp, x);
  f25519_copy(r, tmp);
}

/* Raise x to the power of (p-5)/8 = 2^252-3, using s for temporary
 * storage.
 */
static void exp2523(f25519_t &r, const f25519_t &x, f25519_t &s)
{
  /* This number is a 252-bit number with the binary expansion:
   *
   *     111111... 01
   */

  /* 1 1 */
  f25519_mul__distinct(r, x, x);
  f25519_mul__distinct(s, r, x);

  /* 1 x 248 */
  for (int i = 0; i < 248; i++) {
    f25519_mul__distinct(r, s, s);
    f25519_mul__distinct(s, r, x);
  }

  /* 0 */
  f25519_mul__distinct(r, s, s);

  /* 1 */
  f25519_mul__distinct(s, r, r);
  f25519_mul__distinct(r, s, x);
}


/* Compute one of the square roots of the field element, if the element
 * is square. The other square is -r.
 *
 * If the input is not square, the returned value is a valid field
 * element, but not the correct answer. If you don't already know that
 * your element is square, you should square the return value and test.
 */
void f25519_sqrt(f25519_t &r, const f25519_t &a)
{
  f25519_t v,i,x,y;

  /* v = (2a)^((p-5)/8) [x = 2a] */
  f25519_mul_c(x, a, 2);
  exp2523(v, x, y);

  /* i = 2av^2 - 1 */
  f25519_mul__distinct(y, v, v);
  f25519_mul__distinct(i, x, y);
  f25519_load(y, 1);
  f25519_sub(i, i, y);

  /* r = avi */
  f25519_mul__distinct(x, v, a);
  f25519_mul__distinct(r, x, i);
}

/* Curve25519 has the equation over F(p = 2^255-19):
 *
 *    y^2 = x^3 + 486662x^2 + x
 *
 * 486662 = 4A+2, where A = 121665. This is a Montgomery curve.
 *
 * For more information, see:
 *
 *    Bernstein, D.J. (2006) "Curve25519: New Diffie-Hellman speed
 *    records". Document ID: 4230efdfa673480fc079449d90f322c0.
 */



const f25519_t c25519_base_x(9);

const uint8_t c25519_base_x_bin[32] = {9};

/* Double an X-coordinate */
static void xc_double(f25519_t &x3, f25519_t &z3,
		      const f25519_t &x1, const f25519_t &z1)
{
  /* Explicit formulas database: dbl-1987-m
   *
   * source 1987 Montgomery "Speeding the Pollard and elliptic
   *   curve methods of factorization", page 261, fourth display
   * compute X3 = (X1^2-Z1^2)^2
   * compute Z3 = 4 X1 Z1 (X1^2 + a X1 Z1 + Z1^2)
   */
  f25519_t x1sq;
  f25519_t z1sq;
  f25519_t x1z1;
  f25519_t a;

  f25519_mul__distinct(x1sq, x1, x1);
  f25519_mul__distinct(z1sq, z1, z1);
  f25519_mul__distinct(x1z1, x1, z1);

  f25519_sub(a, x1sq, z1sq);
  f25519_mul__distinct(x3, a, a);

  f25519_mul_c(a, x1z1, 486662);
  f25519_add(a, x1sq, a);
  f25519_add(a, z1sq, a);
  f25519_mul__distinct(x1sq, x1z1, a);
  f25519_mul_c(z3, x1sq, 4);
}

/* Differential addition */
static void xc_diffadd(f25519_t &x5, f25519_t &z5,
		       const f25519_t &x1, const f25519_t &z1,
		       const f25519_t &x2, const f25519_t &z2,
		       const f25519_t &x3, const f25519_t &z3)
{
  /* Explicit formulas database: dbl-1987-m3
   *
   * source 1987 Montgomery "Speeding the Pollard and elliptic curve
   *   methods of factorization", page 261, fifth display, plus
   *   common-subexpression elimination
   * compute A = X2+Z2
   * compute B = X2-Z2
   * compute C = X3+Z3
   * compute D = X3-Z3
   * compute DA = D A
   * compute CB = C B
   * compute X5 = Z1(DA+CB)^2
   * compute Z5 = X1(DA-CB)^2
   */
  f25519_t da;
  f25519_t cb;
  f25519_t a;
  f25519_t b;

  f25519_add(a, x2, z2);
  f25519_sub(b, x3, z3); /* D */
  f25519_mul__distinct(da, a, b);

  f25519_sub(b, x2, z2);
  f25519_add(a, x3, z3); /* C */
  f25519_mul__distinct(cb, a, b);

  f25519_add(a, da, cb);
  f25519_mul__distinct(b, a, a);
  f25519_mul__distinct(x5, z1, b);

  f25519_sub(a, da, cb);
  f25519_mul__distinct(b, a, a);
  f25519_mul__distinct(z5, x1, b);
}

/* X-coordinate scalar multiply: given the X-coordinate of q, return the
 * X-coordinate of e*q.
 *
 * result and q are field elements. e is an exponent.
 */
/* X-coordinate of the base point */

void c25519_smult(f25519_t &result, const f25519_t &q, const unsigned char *e)
{
  /* Current point: P_m */
  f25519_t xm,zm(1);

  /* Predecessor: P_(m-1) */
  f25519_t xm1(1);
  f25519_t zm1(0);

  /* Note: bit 254 is assumed to be 1 */
  f25519_copy(xm, q);

  for (int i = 253; i >= 0; i--) {
    const int bit = getbit(e,i);
    f25519_t xms,zms;

    /* From P_m and P_(m-1), compute P_(2m) and P_(2m-1) */
    xc_diffadd(xm1, zm1, q, f25519_one, xm, zm, xm1, zm1);
    xc_double(xm, zm, xm, zm);

    /* Compute P_(2m+1) */
    xc_diffadd(xms, zms, xm1, zm1, xm, zm, q, f25519_one);

    /* Select:
     *   bit = 1 --> (P_(2m+1), P_(2m))
     *   bit = 0 --> (P_(2m), P_(2m-1))
     */
    f25519_select(xm1, xm1, xm, bit);
    f25519_select(zm1, zm1, zm, bit);
    f25519_select(xm, xm, xms, bit);
    f25519_select(zm, zm, zms, bit);
  }

  /* Freeze out of projective coordinates */
  f25519_inv__distinct(zm1, zm);
  f25519_mul__distinct(result, zm1, xm);
}

void c25519_smult(f25519_t &result, const f25519_t &q, const f25519_t &e){
  unsigned char t[32];
  e.writebin(t);
  c25519_smult(result,q,t);
}


void c25519_smult(uint8_t *result_, const uint8_t *q_, const uint8_t *e_)
{
  f25519_t result,q,e;

  q.set(q_);
  e.set(e_);
  c25519_smult(result,q,e);
  result.writebin(result_);
}


/* Montgomery <-> Edwards isomorphism
 * Daniel Beer <dlbeer@gmail.com>, 18 Jan 2014
 *
 * This file is in the public domain.
 */

void morph25519_e2m(f25519_t &montgomery_x, const f25519_t &edwards_y);

/* Return a parity bit for the Edwards X coordinate */
static inline int morph25519_eparity(const f25519_t &edwards_x)
{
  return edwards_x.parity();
}

/* Convert a Montgomery X and a parity bit to an Edwards X/Y. Returns
 * non-zero if successful.
 */
uint8_t morph25519_m2e(f25519_t &ex, f25519_t &ey,
		       const f25519_t &mx, int parity);

void morph25519_e2m(f25519_t &montgomery, const f25519_t &y)
{
  f25519_t yplus,yminus;

  f25519_sub(yplus, f25519_one, y);
  f25519_inv__distinct(yminus, yplus);
  f25519_add(yplus, f25519_one, y);
  f25519_mul__distinct(montgomery, yplus, yminus);
}

static void mx2ey(f25519_t &ey, const f25519_t &mx)
{
  f25519_t n,d;

  f25519_add(n, mx, f25519_one);
  f25519_inv__distinct(d, n);
  f25519_sub(n, mx, f25519_one);
  f25519_mul__distinct(ey, n, d);
}

static uint8_t ey2ex(f25519_t &x, const f25519_t &y, int parity)
{
  f25519_t d;
  static const uint8_t dd[32] = {
    0xa3, 0x78, 0x59, 0x13, 0xca, 0x4d, 0xeb, 0x75,
    0xab, 0xd8, 0x41, 0x41, 0x4d, 0x0a, 0x70, 0x00,
    0x98, 0xe8, 0x79, 0x77, 0x79, 0x40, 0xc7, 0x8c,
    0x73, 0xfe, 0x6f, 0x2b, 0xee, 0x6c, 0x03, 0x52
  };
  d.set(dd);

  f25519_t a,b,c;

  /* Compute c = y^2 */
  f25519_mul__distinct(c, y, y);

  /* Compute b = (1+dy^2)^-1 */
  f25519_mul__distinct(b, c, d);
  f25519_add(a, b, f25519_one);
  f25519_inv__distinct(b, a);

  /* Compute a = y^2-1 */
  f25519_sub(a, c, f25519_one);

  /* Compute c = a*b = (y^2+1)/(1-dy^2) */
  f25519_mul__distinct(c, a, b);

  /* Compute a, b = +/-sqrt(c), if c is square */
  f25519_sqrt(a, c);
  f25519_neg(b, a);

  /* Select one of them, based on the parity bit */
  f25519_select(x, a, b, (a.parity() ^ parity));

  /* Verify that x^2 = c */
  f25519_mul__distinct(a, x, x);

  return f25519_eq(a, c);
}

uint8_t morph25519_m2e(f25519_t &ex, f25519_t &ey,
		       const f25519_t &mx, int parity)
{
  uint8_t ok;

  mx2ey(ey, mx);
  ok = ey2ex(ex, ey, parity);


  return ok;
}

/* This is not the Ed25519 signature system. Rather, we're implementing
 * basic operations on the twisted Edwards curve over (Z mod 2^255-19):
 *
 *     -x^2 + y^2 = 1 - (121665/121666)x^2y^2
 *
 * With the positive-x base point y = 4/5.
 *
 * These functions will not leak secret data through timing.
 *
 * For more information, see:
 *
 *     Bernstein, D.J. & Lange, T. (2007) "Faster addition and doubling on
 *     elliptic curves". Document ID: 95616567a6ba20f575c5f25e7cebaf83.
 *
 *     Hisil, H. & Wong, K K. & Carter, G. & Dawson, E. (2008) "Twisted
 *     Edwards curves revisited". Advances in Cryptology, ASIACRYPT 2008,
 *     Vol. 5350, pp. 326-343.
 */


struct ed25519_pt {
  void aff() const {
    printf("point:\n");
    x.aff();
    y.aff();
    z.aff();
    t.aff();
  }
  f25519_t x,y,t,z;
};

extern const struct ed25519_pt ed25519__base;
extern const struct ed25519_pt ed25519__neutral;

/* Convert between projective and affine coordinates (x/y in F25519) */
void ed25519_project(struct ed25519_pt *p,
		     const f25519_t &x, const f25519_t &y);

void ed25519_unproject(f25519_t &x, f25519_t &y,
		       const struct ed25519_pt *p);

/* Compress/uncompress points. try_unpack() will check that the
 * compressed point is on the curve, returning 1 if the unpacked point
 * is valid, and 0 otherwise.
 */

uint8_t ed25519_try_unpack(f25519_t &x, f25519_t &y, const f25519_t &c);

/* Add, double and scalar multiply */


/* Prepare an exponent by clamping appropriate bits */
static inline void ed25519_prepare(unsigned char *e)
{
  e[0] &= 0xf8;
  e[31] &= 0x7f;
  e[31] |= 0x40;
}

/* Order of the group generated by the base point */
static inline void ed25519_copy(struct ed25519_pt *dst,
				const struct ed25519_pt *src)
{
  *dst=*src;
}

void ed25519_add(struct ed25519_pt *r,
		 const struct ed25519_pt *a, const struct ed25519_pt *b);
void ed25519_double(struct ed25519_pt *r, const struct ed25519_pt *a);
void ed25519_smult(struct ed25519_pt *r, const struct ed25519_pt *a,
		   const unsigned char *e);

void ed25519_smult(struct ed25519_pt *r, const struct ed25519_pt *a,
		   const f25519_t &e) {
  unsigned char t[32];
  e.writebin(t);
  ed25519_smult(r,a,t);
}

/* Base point is (numbers wrapped):
 *
 *     x = 151122213495354007725011514095885315114
 *         54012693041857206046113283949847762202
 *     y = 463168356949264781694283940034751631413
 *         07993866256225615783033603165251855960
 *
 * y is derived by transforming the original Montgomery base (u=9). x
 * is the corresponding positive coordinate for the new curve equation.
 * t is x*y.
 */

ed25519_pt ed25519_base,ed25519_neutral;
f25519_t ed25519_d;
f25519_t ed25519_k;

/* Conversion to and from projective coordinates */
void ed25519_project(struct ed25519_pt *p,
		     const f25519_t &x, const f25519_t &y)
{
  f25519_copy(p->x, x);
  f25519_copy(p->y, y);
  f25519_load(p->z, 1);
  f25519_mul__distinct(p->t, x, y);
}

void ed25519_unproject(f25519_t &x, f25519_t &y,
		       const struct ed25519_pt *p)
{
  f25519_t z1;
  
  f25519_inv__distinct(z1, p->z);
	
  f25519_mul__distinct(x, p->x, z1);
  f25519_mul__distinct(y, p->y, z1);
  
}

/* Compress/uncompress points. We compress points by storing the x
 * coordinate and the parity of the y coordinate.
 *
 * Rearranging the curve equation, we obtain explicit formulae for the
 * coordinates:
 *
 *     x = sqrt((y^2-1) / (1+dy^2))
 *     y = sqrt((x^2+1) / (1-dx^2))
 *
 * Where d = (-121665/121666), or:
 *
 *     d = 370957059346694393431380835087545651895
 *         42113879843219016388785533085940283555
 */

void ed25519_pack(unsigned char *cc, const f25519_t &x, const f25519_t &y)
{
  y.writebin(cc);
  setbit(cc,255,x.parity());
}

uint8_t ed25519_try_unpack(f25519_t &x, f25519_t &y, const unsigned char *comp)
{
  const int parity = getbit(comp,255);
  f25519_t a,b,c;

  /* Unpack y */
  unsigned char tmp[32];
  memcpy(tmp,comp,32);
  setbit(tmp,255,0);
  y.set(tmp);

  /* Compute c = y^2 */
  f25519_mul__distinct(c, y, y);

  /* Compute b = (1+dy^2)^-1 */
  f25519_mul__distinct(b, c, ed25519_d);
  f25519_add(a, b, f25519_one);
  f25519_inv__distinct(b, a);

  /* Compute a = y^2-1 */
  f25519_sub(a, c, f25519_one);

  /* Compute c = a*b = (y^2-1)/(1-dy^2) */
  f25519_mul__distinct(c, a, b);

  /* Compute a, b = +/-sqrt(c), if c is square */
  f25519_sqrt(a, c);
  f25519_neg(b, a);

  /* Select one of them, based on the compressed parity bit */
  f25519_select(x, a, b, (a.parity() ^ parity) & 1);

  /* Verify that x^2 = c */
  f25519_mul__distinct(a, x, x);

  return f25519_eq(a, c);
}

void ed25519_add(struct ed25519_pt *r,
		 const struct ed25519_pt *p1, const struct ed25519_pt *p2)
{
  /* Explicit formulas database: add-2008-hwcd-3
   *
   * source 2008 Hisil--Wong--Carter--Dawson,
   *     http://eprint.iacr.org/2008/522, Section 3.1
   * appliesto extended-1
   * parameter k
   * assume k = 2 d
   * compute A = (Y1-X1)(Y2-X2)
   * compute B = (Y1+X1)(Y2+X2)
   * compute C = T1 k T2
   * compute D = Z1 2 Z2
   * compute E = B - A
   * compute F = D - C
   * compute G = D + C
   * compute H = B + A
   * compute X3 = E F
   * compute Y3 = G H
   * compute T3 = E H
   * compute Z3 = F G
   */
  f25519_t a,b,c,d,e,f,g,h;
  
  /* A = (Y1-X1)(Y2-X2) */
  f25519_sub(c, p1->y, p1->x);
  f25519_sub(d, p2->y, p2->x);
  f25519_mul__distinct(a, c, d);
	
  /* B = (Y1+X1)(Y2+X2) */
  f25519_add(c, p1->y, p1->x);
  f25519_add(d, p2->y, p2->x);
  f25519_mul__distinct(b, c, d);

  /* C = T1 k T2 */
  f25519_mul__distinct(d, p1->t, p2->t);
  f25519_mul__distinct(c, d, ed25519_k);
	
  /* D = Z1 2 Z2 */
  f25519_mul__distinct(d, p1->z, p2->z);
  f25519_add(d, d, d);
	
  /* E = B - A */
  f25519_sub(e, b, a);
	
  /* F = D - C */
  f25519_sub(f, d, c);

  /* G = D + C */
  f25519_add(g, d, c);

  /* H = B + A */
  f25519_add(h, b, a);

  /* X3 = E F */
  f25519_mul__distinct(r->x, e, f);
	
  /* Y3 = G H */
  f25519_mul__distinct(r->y, g, h);

  /* T3 = E H */
  f25519_mul__distinct(r->t, e, h);

  /* Z3 = F G */
  f25519_mul__distinct(r->z, f, g);
}


void ed25519_double(struct ed25519_pt *r, const struct ed25519_pt *p)
{
  /* Explicit formulas database: dbl-2008-hwcd
   *
   * source 2008 Hisil--Wong--Carter--Dawson,
   *     http://eprint.iacr.org/2008/522, Section 3.3
   * compute A = X1^2
   * compute B = Y1^2
   * compute C = 2 Z1^2
   * compute D = a A
   * compute E = (X1+Y1)^2-A-B
   * compute G = D + B
   * compute F = G - C
   * compute H = D - B
   * compute X3 = E F
   * compute Y3 = G H
   * compute T3 = E H
   * compute Z3 = F G
   */
  f25519_t a,b,c,d,e,f,g,h;

  /* A = X1^2 */
  f25519_mul__distinct(a, p->x, p->x);
	
  /* B = Y1^2 */
  f25519_mul__distinct(b, p->y, p->y);
	
  /* C = 2 Z1^2 */
  f25519_mul__distinct(c, p->z, p->z);
  f25519_add(c, c, c);
	
  /* D = a A (alter sign) */
  /* E = (X1+Y1)^2-A-B */
  f25519_add(f, p->x, p->y);

  f25519_mul__distinct(e, f, f);
  f25519_sub(e, e, a);
  f25519_sub(e, e, b);
	
  /* G = D + B */
  f25519_sub(g, b, a);

  /* F = G - C */
  f25519_sub(f, g, c);

  /* H = D - B */
  f25519_neg(h, b);
  f25519_sub(h, h, a);
	
  /* X3 = E F */
  f25519_mul__distinct(r->x, e, f);
	
  /* Y3 = G H */
  f25519_mul__distinct(r->y, g, h);

  /* T3 = E H */
  f25519_mul__distinct(r->t, e, h);

  /* Z3 = F G */
  f25519_mul__distinct(r->z, f, g);
}

void ed25519_smult(struct ed25519_pt *r_out, const struct ed25519_pt *p,
		   const unsigned char *e)
{
  struct ed25519_pt r;

  ed25519_copy(&r, &ed25519_neutral);
	
  for (int i = 255; i >= 0; i--) {
    const uint8_t bit = getbit(e,i);
    struct ed25519_pt s;


    ed25519_double(&r, &r);

    ed25519_add(&s, &r, p);

    f25519_select(r.x, r.x, s.x, bit);
    f25519_select(r.y, r.y, s.y, bit);
    f25519_select(r.z, r.z, s.z, bit);
    f25519_select(r.t, r.t, s.t, bit);
  }
	
  ed25519_copy(r_out, &r);
}

/* This is the Ed25519 signature system, as described in:
 *
 *     Daniel J. Bernstein, Niels Duif, Tanja Lange, Peter Schwabe, Bo-Yin
 *     Yang. High-speed high-security signatures. Journal of Cryptographic
 *     Engineering 2 (2012), 77-89. Document ID:
 *     a1a62a2f76d23f65d622484ddd09caf8. URL:
 *     http://cr.yp.to/papers.html#ed25519. Date: 2011.09.26.
 *
 * The format and calculation of signatures is compatible with the
 * Ed25519 implementation in SUPERCOP. Note, however, that our secret
 * keys are half the size: we don't store a copy of the public key in
 * the secret key (we generate it on demand).
 */

/* Any string of 32 random bytes is a valid secret key. There is no
 * clamping of bits, because we don't use the key directly as an
 * exponent (the exponent is derived from part of a key expansion).
 */

/* Given a secret key, produce the public key (a packed Edwards-curve
 * point).
 */

void edsign_sec_to_pub(f25519_t &pub, const f25519_t &secret);

static const uint8_t ed25519_order[32] = {
  0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
  0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};


static void expand_key(unsigned char *expanded, const uint8_t *secret)
{
  mbedtls_sha512_context ctx;
  mbedtls_sha512_init(&ctx);
  mbedtls_sha512_starts(&ctx,0);
  mbedtls_sha512_update(&ctx, secret, 32);
  mbedtls_sha512_finish(&ctx,expanded);
  mbedtls_sha512_free(&ctx);
  ed25519_prepare(expanded);
}

static uint8_t upp(struct ed25519_pt *p, const unsigned char *packed)
{
  f25519_t x,y;
  uint8_t ok = ed25519_try_unpack(x, y, packed);
  
  ed25519_project(p, x, y);
  return ok;
}

static void pp( unsigned char *packed, const struct ed25519_pt *p)
{
  f25519_t x,y;

  ed25519_unproject(x, y, p);
  ed25519_pack(packed, x, y);
}

static void sm_pack(unsigned char *r, const f25519_t &k)
{
  struct ed25519_pt p;
 
  ed25519_smult(&p, &ed25519_base, k);
  pp(r, &p);
}

void edsign_sec_to_pub(unsigned char *pub, const unsigned char *secret)
{
  uint8_t expanded[64];
  f25519_t e;

  expand_key(expanded, secret);
  e.set(expanded);
  sm_pack(pub, e);
}


void mpi_initload(mbedtls_mpi *x,const unsigned char *n,int len)
{
  unsigned char r[64];
  for(int u=0;u<len;u++)
    r[u]=n[len-1-u];
  mbedtls_mpi_init(x);
  mbedtls_mpi_read_binary(x,r,len);
}

void mpi_writebin(mbedtls_mpi *x,unsigned char *n,int len)
{
  unsigned char r[64];
  mbedtls_mpi_write_binary(x,r,len);
  for(int u=0;u<len;u++)
    n[u]=r[len-1-u];
}


void fprime_add(uint8_t *r, const uint8_t *a, const uint8_t *modulus)
{
  mbedtls_mpi rn,an,mn;

  mpi_initload(&rn,r,32);
  mpi_initload(&an,a,32);
  mpi_initload(&mn,modulus,32);

  mbedtls_mpi_add_mpi(&rn,&rn,&an);
  mbedtls_mpi_mod_mpi(&rn,&rn,&mn);

  mpi_writebin(&rn,r,32);

  mbedtls_mpi_free(&an);
  mbedtls_mpi_free(&rn);
  mbedtls_mpi_free(&mn);
}

void fprime_mul(uint8_t *r, const uint8_t *a, const uint8_t *b,
		const uint8_t *modulus)
{
  mbedtls_mpi an,bn,mn;

  mpi_initload(&bn,b,32);
  mpi_initload(&an,a,32);
  mpi_initload(&mn,modulus,32);

  mbedtls_mpi_mul_mpi(&an,&an,&bn);
  mbedtls_mpi_mod_mpi(&an,&an,&mn);

  mpi_writebin(&an,r,32);

  mbedtls_mpi_free(&an);
  mbedtls_mpi_free(&bn);
  mbedtls_mpi_free(&mn);
}

void fprime_from_bytes(uint8_t *n,
		       const uint8_t *x, size_t len,
		       const uint8_t *modulus)
{
  mbedtls_mpi bnx,bnm;
  mpi_initload(&bnx,x,len);
  mpi_initload(&bnm,modulus,32);

  mbedtls_mpi_mod_mpi(&bnx,&bnx,&bnm);

  mpi_writebin(&bnx,n,32);
  mbedtls_mpi_free(&bnm);
  mbedtls_mpi_free(&bnx);
}

static void hash_with_prefix(uint8_t *out_fp,
			     uint8_t *init_block, unsigned int prefix_size,
			     const uint8_t *message, size_t len)
{
  mbedtls_sha512_context ctx;
  mbedtls_sha512_init(&ctx);
  mbedtls_sha512_starts(&ctx,0);
  mbedtls_sha512_update(&ctx, init_block, prefix_size);
  mbedtls_sha512_update(&ctx, message, len);
  mbedtls_sha512_finish(&ctx, init_block);
  mbedtls_sha512_free(&ctx);
  fprime_from_bytes(out_fp, init_block, 64, ed25519_order);
}


static void hash_message(uint8_t *z, const uint8_t *r, const uint8_t *a,
			 const uint8_t *m, size_t len)
{
  uint8_t block[128];

  memcpy(block, r, 32);
  memcpy(block + 32, a, 32);
  hash_with_prefix(z, block, 64, m, len);
}

uint8_t edsign_verify(const uint8_t *signature, const uint8_t *pub,
		      const uint8_t *message, size_t len)
{
  struct ed25519_pt p;
  struct ed25519_pt q;
  unsigned char lhs[32];
  unsigned char rhs[32];
  unsigned char z[32];
  uint8_t ok = 1;

  /* Compute z = H(R, A, M) */
  hash_message(z, signature, pub, message, len);

  /* sB = (ze + k)B = ... */
  
  f25519_t si2;
  si2.set(signature + 32);
  sm_pack(lhs, si2);

  /* ... = zA + R */
  
  ok &= upp(&p, pub);
  ed25519_smult(&p, &p, z);
  ok &= upp(&q, signature);
  ed25519_add(&p, &p, &q);
  pp(rhs, &p);

  /* Equal? */
  return ok & (0==memcmp(lhs, rhs,32));
}

static void generate_k(uint8_t *k, const uint8_t *kgen_key,
		       const uint8_t *message, size_t len)
{
  uint8_t block[128]; /*SHA512_BLOCK_SIZE*/

  memcpy(block, kgen_key, 32);
  hash_with_prefix(k, block, 32, message, len);
}


void edsign_sign(uint8_t *signature, const uint8_t *pub,
		 const uint8_t *secret,
		 const uint8_t *message, size_t len)
{
  uint8_t expanded[64]; /*EXPANDED_SIZE*/
  uint8_t e[32];
  uint8_t s[32];
  uint8_t k[32];
  uint8_t z[32];

  expand_key(expanded, secret);

  /* Generate k and R = kB */
  generate_k(k, expanded + 32, message, len);
  f25519_t sk;
  sk.set(k);
  sm_pack(signature, sk);

  /* Compute z = H(R, A, M) */
  hash_message(z, signature, pub, message, len);

  /* Obtain e */
  fprime_from_bytes(e, expanded, 32, ed25519_order);

  /* Compute s = ze + k */
  fprime_mul(s, z, e, ed25519_order);
  fprime_add(s, k, ed25519_order);

  memcpy(signature + 32, s, 32);
}

/* INIT */

void init_ed25519() {
  const unsigned char x[32]={
    0x1a, 0xd5, 0x25, 0x8f, 0x60, 0x2d, 0x56, 0xc9,
    0xb2, 0xa7, 0x25, 0x95, 0x60, 0xc7, 0x2c, 0x69,
    0x5c, 0xdc, 0xd6, 0xfd, 0x31, 0xe2, 0xa4, 0xc0,
    0xfe, 0x53, 0x6e, 0xcd, 0xd3, 0x36, 0x69, 0x21
  };
  const unsigned char y[32] = {
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
  };
  const unsigned char t[32] = {
    0xa3, 0xdd, 0xb7, 0xa5, 0xb3, 0x8a, 0xde, 0x6d,
    0xf5, 0x52, 0x51, 0x77, 0x80, 0x9f, 0xf0, 0x20,
    0x7d, 0xe3, 0xab, 0x64, 0x8e, 0x4e, 0xea, 0x66,
    0x65, 0x76, 0x8b, 0xd7, 0x0f, 0x5f, 0x87, 0x67
  };

  static const uint8_t ed25519_dd[32] = {
    0xa3, 0x78, 0x59, 0x13, 0xca, 0x4d, 0xeb, 0x75,
    0xab, 0xd8, 0x41, 0x41, 0x4d, 0x0a, 0x70, 0x00,
    0x98, 0xe8, 0x79, 0x77, 0x79, 0x40, 0xc7, 0x8c,
    0x73, 0xfe, 0x6f, 0x2b, 0xee, 0x6c, 0x03, 0x52
  };

  /* k = 2d */
  static const uint8_t ed25519_kk[32] = {
    0x59, 0xf1, 0xb2, 0x26, 0x94, 0x9b, 0xd6, 0xeb,
    0x56, 0xb1, 0x83, 0x82, 0x9a, 0x14, 0xe0, 0x00,
    0x30, 0xd1, 0xf3, 0xee, 0xf2, 0x80, 0x8e, 0x19,
    0xe7, 0xfc, 0xdf, 0x56, 0xdc, 0xd9, 0x06, 0x24
  };

  ed25519_d.set(ed25519_dd);
  ed25519_k.set(ed25519_kk);
  
  ed25519_base.x.set(x);  
  ed25519_base.y.set(y);  
  ed25519_base.t.set(t);  
  ed25519_base.z.set(1);

  ed25519_neutral.x.set(0);
  ed25519_neutral.y.set(1);
  ed25519_neutral.z.set(0);
  ed25519_neutral.t.set(1);
};

int auto_init_c25519=(init_ed25519(),0);


