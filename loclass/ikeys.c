/*****************************************************************************
 * WARNING
 *
 * THIS CODE IS CREATED FOR EXPERIMENTATION AND EDUCATIONAL USE ONLY. 
 * 
 * USAGE OF THIS CODE IN OTHER WAYS MAY INFRINGE UPON THE INTELLECTUAL 
 * PROPERTY OF OTHER PARTIES, SUCH AS INSIDE SECURE AND HID GLOBAL, 
 * AND MAY EXPOSE YOU TO AN INFRINGEMENT ACTION FROM THOSE PARTIES. 
 * 
 * THIS CODE SHOULD NEVER BE USED TO INFRINGE PATENTS OR INTELLECTUAL PROPERTY RIGHTS. 
 *
 *****************************************************************************
 *
 * This file is part of loclass. It is a reconstructon of the cipher engine
 * used in iClass, and RFID techology.
 *
 * The implementation is based on the work performed by
 * Flavio D. Garcia, Gerhard de Koning Gans, Roel Verdult and
 * Milosch Meriac in the paper "Dismantling IClass".
 *
 * Copyright (C) 2014 Martin Holst Swende
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or, at your option, any later version. 
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with loclass.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 ****************************************************************************/

/**


From "Dismantling iclass":
	This section describes in detail the built-in key diversification algorithm of iClass.
	Besides the obvious purpose of deriving a card key from a master key, this
	algorithm intends to circumvent weaknesses in the cipher by preventing the
	usage of certain ‘weak’ keys. In order to compute a diversified key, the iClass
	reader first encrypts the card identity id with the master key K, using single
	DES. The resulting ciphertext is then input to a function called hash0 which
	outputs the diversified key k.

	k = hash0(DES enc (id, K))

	Here the DES encryption of id with master key K outputs a cryptogram c
	of 64 bits. These 64 bits are divided as c = x, y, z [0] , . . . , z [7] ∈ F 82 × F 82 × (F 62 ) 8
	which is used as input to the hash0 function. This function introduces some
	obfuscation by performing a number of permutations, complement and modulo
	operations, see Figure 2.5. Besides that, it checks for and removes patterns like
	similar key bytes, which could produce a strong bias in the cipher. Finally, the
	output of hash0 is the diversified card key k = k [0] , . . . , k [7] ∈ (F 82 ) 8 .


**/


#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "fileutils.h"
#include "cipherutils.h"
#include "des.h"

uint8_t pi[35] = {0x0F,0x17,0x1B,0x1D,0x1E,0x27,0x2B,0x2D,0x2E,0x33,0x35,0x39,0x36,0x3A,0x3C,0x47,0x4B,0x4D,0x4E,0x53,0x55,0x56,0x59,0x5A,0x5C,0x63,0x65,0x66,0x69,0x6A,0x6C,0x71,0x72,0x74,0x78};

static des_context ctx_enc = {DES_ENCRYPT,{0}};
static des_context ctx_dec = {DES_DECRYPT,{0}};

static int debug_print = 0;

/**
 * @brief The key diversification algorithm uses 6-bit bytes.
 * This implementation uses 64 bit uint to pack seven of them into one
 * variable. When they are there, they are placed as follows:
 * XXXX XXXX N0 .... N7, occupying the lsat 48 bits.
 *
 * This function picks out one from such a collection
 * @param all
 * @param n bitnumber
 * @return
 */
uint8_t getSixBitByte(uint64_t c, int n)
{
	return (c >> (42-6*n)) & 0x3F;
}

/**
 * @brief Puts back a six-bit 'byte' into a uint64_t.
 * @param c buffer
 * @param z the value to place there
 * @param n bitnumber.
 */
void pushbackSixBitByte(uint64_t *c, uint8_t z, int n)
{
	//0x XXXX YYYY ZZZZ ZZZZ ZZZZ
	//             ^z0         ^z7
	//z0:  1111 1100 0000 0000

	uint64_t masked = z & 0x3F;
	uint64_t eraser = 0x3F;
	masked <<= 42-6*n;
	eraser <<= 42-6*n;

	//masked <<= 6*n;
	//eraser <<= 6*n;

	eraser = ~eraser;
	(*c) &= eraser;
	(*c) |= masked;

}
/**
 * @brief Swaps the z-values.
 * If the input value has format XYZ0Z1...Z7, the output will have the format
 * XYZ7Z6...Z0 instead
 * @param c
 * @return
 */
uint64_t swapZvalues(uint64_t c)
{
	uint64_t newz = 0;
	pushbackSixBitByte(&newz, getSixBitByte(c,0),7);
	pushbackSixBitByte(&newz, getSixBitByte(c,1),6);
	pushbackSixBitByte(&newz, getSixBitByte(c,2),5);
	pushbackSixBitByte(&newz, getSixBitByte(c,3),4);
	pushbackSixBitByte(&newz, getSixBitByte(c,4),3);
	pushbackSixBitByte(&newz, getSixBitByte(c,5),2);
	pushbackSixBitByte(&newz, getSixBitByte(c,6),1);
	pushbackSixBitByte(&newz, getSixBitByte(c,7),0);
	newz |= (c & 0xFFFF000000000000);
	return newz;
}

/**
* @return 4 six-bit bytes chunked into a uint64_t,as 00..00a0a1a2a3
*/
uint64_t ck(int i, int j, uint64_t z)
{

	if(i == 1 && j == -1)
	{
		// ck(1, −1, z [0] . . . z [3] ) = z [0] . . . z [3]
		return z;

	}else if( j == -1)
	{
		// ck(i, −1, z [0] . . . z [3] ) = ck(i − 1, i − 2, z [0] . . . z [3] )
		return ck(i-1,i-2, z);
	}

	if(getSixBitByte(z,i) == getSixBitByte(z,j))
	{

		//ck(i, j − 1, z [0] . . . z [i] ← j . . . z [3] )
		uint64_t newz = 0;
		int c;
		for(c = 0; c < 4 ;c++)
		{
			uint8_t val = getSixBitByte(z,c);
			if(c == i)
			{
				pushbackSixBitByte(&newz, j, c);
			}else
			{
				pushbackSixBitByte(&newz, val, c);
			}
		}
		return ck(i,j-1,newz);
	}else
	{
		return ck(i,j-1,z);
	}
}
/**

	Definition 8.
	Let the function check : (F 62 ) 8 → (F 62 ) 8 be defined as
	check(z [0] . . . z [7] ) = ck(3, 2, z [0] . . . z [3] ) · ck(3, 2, z [4] . . . z [7] )

	where ck : N × N × (F 62 ) 4 → (F 62 ) 4 is defined as

		ck(1, −1, z [0] . . . z [3] ) = z [0] . . . z [3]
		ck(i, −1, z [0] . . . z [3] ) = ck(i − 1, i − 2, z [0] . . . z [3] )
		ck(i, j, z [0] . . . z [3] ) =
		ck(i, j − 1, z [0] . . . z [i] ← j . . . z [3] ),  if z [i] = z [j] ;
		ck(i, j − 1, z [0] . . . z [3] ), otherwise

	otherwise.
**/

uint64_t check(uint64_t z)
{
	//These 64 bits are divided as c = x, y, z [0] , . . . , z [7]

	// ck(3, 2, z [0] . . . z [3] )
	uint64_t ck1 = ck(3,2, z );

	// ck(3, 2, z [4] . . . z [7] )
	uint64_t ck2 = ck(3,2, z << 24);

	//The ck function will place the values
	// in the middle of z.
	ck1 &= 0x00000000FFFFFF000000;
	ck2 &= 0x00000000FFFFFF000000;

	return ck1 | ck2 >> 24;

}

void permute(BitstreamIn *p_in, uint64_t z,int l,int r, BitstreamOut* out)
{
	if(bitsLeft(p_in) == 0)
	{
		return;
	}
	bool pn = tailBit(p_in);
	if( pn ) // pn = 1
	{
		uint8_t zl = getSixBitByte(z,l);

		push6bits(out, zl+1);
		permute(p_in, z, l+1,r, out);
	}else // otherwise
	{
		uint8_t zr = getSixBitByte(z,r);

		push6bits(out, zr);
		permute(p_in,z,l,r+1,out);
	}
}
void printbegin()
{
	if(debug_print <2)
		return ;

	prnlog("          | x| y|z0|z1|z2|z3|z4|z5|z6|z7|");
}

void printState(char* desc, uint64_t c)
{
	if(debug_print < 2)
		return ;

	printf("%s : ", desc);
	uint8_t x = 	(c & 0xFF00000000000000 ) >> 56;
	uint8_t y = 	(c & 0x00FF000000000000 ) >> 48;
	printf("  %02x %02x", x,y);
	int i ;
	for(i =0 ; i < 8 ; i++)
	{
		printf(" %02x", getSixBitByte(c,i));
	}
	printf("\n");
}

/**
 * @brief
 *Definition 11. Let the function hash0 : F 82 × F 82 × (F 62 ) 8 → (F 82 ) 8 be defined as
 *	hash0(x, y, z [0] . . . z [7] ) = k [0] . . . k [7] where
 * z'[i] = (z[i] mod (63-i)) + i	i =  0...3
 * z'[i+4] = (z[i+4] mod (64-i)) + i	i =  0...3
 * ẑ = check(z');
 * @param c
 * @param k this is where the diversified key is put (should be 8 bytes)
 * @return
 */
void hash0(uint64_t c, uint8_t k[8])
{
	c = swapZvalues(c);

	printbegin();
	printState("origin",c);
	//These 64 bits are divided as c = x, y, z [0] , . . . , z [7]
	// x = 8 bits
	// y = 8 bits
	// z0-z7 6 bits each : 48 bits
	uint8_t x = 	(c & 0xFF00000000000000 ) >> 56;
	uint8_t y = 	(c & 0x00FF000000000000 ) >> 48;
	int n;
	uint8_t zn, zn4, _zn, _zn4;
	uint64_t zP = 0;

	for(n = 0;  n < 4 ; n++)
	{
		zn = getSixBitByte(c,n);

		zn4 = getSixBitByte(c,n+4);

		_zn = (zn % (63-n)) + n;
		_zn4 = (zn4 % (64-n)) + n;


		pushbackSixBitByte(&zP, _zn,n);
		pushbackSixBitByte(&zP, _zn4,n+4);

	}
	printState("0|0|z'",zP);

	uint64_t zCaret = check(zP);
	printState("0|0|z^",zP);


	uint8_t p = pi[x % 35];

	if(x & 1) //Check if x7 is 1
	{
		p = ~p;
	}

	if(debug_print >= 2) prnlog("p:%02x", p);

	BitstreamIn p_in = { &p, 8,0 };
	uint8_t outbuffer[] = {0,0,0,0,0,0,0,0};
	BitstreamOut out = {outbuffer,0,0};
	permute(&p_in,zCaret,0,4,&out);//returns 48 bits? or 6 8-bytes

	//Out is now a buffer containing six-bit bytes, should be 48 bits
	// if all went well
	//Shift z-values down onto the lower segment

    uint64_t zTilde = x_bytes_to_num(outbuffer,8);

	zTilde >>= 16;

	printState("0|0|z~", zTilde);

	int i;
	int zerocounter =0 ;
	for(i =0 ; i < 8  ; i++)
	{

		// the key on index i is first a bit from y
		// then six bits from z,
		// then a bit from p

		// Init with zeroes
		k[i] = 0;
		// First, place yi leftmost in k
		//k[i] |= (y  << i) & 0x80 ;

		// First, place y(7-i) leftmost in k
		k[i] |= (y  << (7-i)) & 0x80 ;



		uint8_t zTilde_i = getSixBitByte(zTilde, i);
		// zTildeI is now on the form 00XXXXXX
		// with one leftshift, it'll be
		// 0XXXXXX0
		// So after leftshift, we can OR it into k
		// However, when doing complement, we need to
		// again MASK 0XXXXXX0 (0x7E)
		zTilde_i <<= 1;

		//Finally, add bit from p or p-mod
		//Shift bit i into rightmost location (mask only after complement)
		uint8_t p_i = p >> i & 0x1;

		if( k[i] )// yi = 1
		{
			//printf("k[%d] +1\n", i);
			k[i] |= ~zTilde_i & 0x7E;
			k[i] |= p_i & 1;
			k[i] += 1;

		}else // otherwise
		{
			k[i] |= zTilde_i & 0x7E;
			k[i] |= (~p_i) & 1;
		}
		if((k[i]  & 1 )== 0)
		{
			zerocounter ++;
		}
	}
}
/**
 * @brief Performs Elite-class key diversification
 * @param csn
 * @param key
 * @param div_key
 */
void diversifyKey(uint8_t csn[8], uint8_t key[8], uint8_t div_key[8])
{

	// Prepare the DES key
	des_setkey_enc( &ctx_enc, key);

	uint8_t crypted_csn[8] = {0};

	// Calculate DES(CSN, KEY)
	des_crypt_ecb(&ctx_enc,csn, crypted_csn);

	//Calculate HASH0(DES))
    uint64_t crypt_csn = x_bytes_to_num(crypted_csn, 8);
	//uint64_t crypted_csn_swapped = swapZvalues(crypt_csn);

	hash0(crypt_csn,div_key);
}





void testPermute()
{

	uint64_t x = 0;
	pushbackSixBitByte(&x,0x00,0);
	pushbackSixBitByte(&x,0x01,1);
	pushbackSixBitByte(&x,0x02,2);
	pushbackSixBitByte(&x,0x03,3);
	pushbackSixBitByte(&x,0x04,4);
	pushbackSixBitByte(&x,0x05,5);
	pushbackSixBitByte(&x,0x06,6);
	pushbackSixBitByte(&x,0x07,7);

	uint8_t mres[8] = { getSixBitByte(x, 0),
						getSixBitByte(x, 1),
						getSixBitByte(x, 2),
						getSixBitByte(x, 3),
						getSixBitByte(x, 4),
						getSixBitByte(x, 5),
						getSixBitByte(x, 6),
						getSixBitByte(x, 7)};
	printarr("input_perm", mres,8);

	uint8_t p = ~pi[0];
	BitstreamIn p_in = { &p, 8,0 };
	uint8_t outbuffer[] = {0,0,0,0,0,0,0,0};
	BitstreamOut out = {outbuffer,0,0};

	permute(&p_in, x,0,4, &out);

    uint64_t permuted = x_bytes_to_num(outbuffer,8);
	//printf("zTilde 0x%"PRIX64"\n", zTilde);
	permuted >>= 16;

	uint8_t res[8] = { getSixBitByte(permuted, 0),
						getSixBitByte(permuted, 1),
						getSixBitByte(permuted, 2),
						getSixBitByte(permuted, 3),
						getSixBitByte(permuted, 4),
						getSixBitByte(permuted, 5),
						getSixBitByte(permuted, 6),
						getSixBitByte(permuted, 7)};
	printarr("permuted", res, 8);
}

//These testcases are
//{ UID , TEMP_KEY, DIV_KEY} using the specific key
typedef struct
{
	uint8_t uid[8];
	uint8_t t_key[8];
	uint8_t div_key[8];
} Testcase;


int testDES(Testcase testcase, des_context ctx_enc, des_context ctx_dec)
{
	uint8_t des_encrypted_csn[8] = {0};
	uint8_t decrypted[8] = {0};
	uint8_t div_key[8] = {0};
	int retval = des_crypt_ecb(&ctx_enc,testcase.uid,des_encrypted_csn);
	retval |= des_crypt_ecb(&ctx_dec,des_encrypted_csn,decrypted);

	if(memcmp(testcase.uid,decrypted,8) != 0)
	{
		//Decryption fail
		prnlog("Encryption <-> Decryption FAIL");
		printarr("Input", testcase.uid, 8);
		printarr("Decrypted", decrypted, 8);
		retval = 1;
	}

	if(memcmp(des_encrypted_csn,testcase.t_key,8) != 0)
	{
		//Encryption fail
		prnlog("Encryption != Expected result");
		printarr("Output", des_encrypted_csn, 8);
		printarr("Expected", testcase.t_key, 8);
		retval = 1;
	}
    uint64_t crypted_csn = x_bytes_to_num(des_encrypted_csn,8);
	hash0(crypted_csn, div_key);

	if(memcmp(div_key, testcase.div_key ,8) != 0)
	{
		//Key diversification fail
		prnlog("Div key != expected result");
		printarr("  csn   ", testcase.uid,8);
		printarr("{csn}   ", des_encrypted_csn,8);
		printarr("hash0   ", div_key, 8);
		printarr("Expected", testcase.div_key, 8);
		retval = 1;

	}
	return retval;
}
bool des_getParityBitFromKey(uint8_t key)
{//The top 7 bits is used
	bool parity = ((key & 0x80) >> 7)
			^ ((key & 0x40) >> 6) ^ ((key & 0x20) >> 5)
			^ ((key & 0x10) >> 4) ^ ((key & 0x08) >> 3)
			^ ((key & 0x04) >> 2) ^ ((key & 0x02) >> 1);
	return !parity;
}


void des_checkParity(uint8_t* key)
{
	int i;
	int fails =0;
	for(i =0 ; i < 8 ; i++)
	{
		bool parity = des_getParityBitFromKey(key[i]);
		if(parity != (key[i] & 0x1))
		{
			fails++;
			prnlog("[+] parity1 fail, byte %d [%02x] was %d, should be %d",i,key[i],(key[i] & 0x1),parity);
		}
	}
	if(fails)
	{
		prnlog("[+] parity fails: %d", fails);
	}else
	{
		prnlog("[+] Key syntax is with parity bits inside each byte");
	}
}

Testcase testcases[] ={

	{{0x8B,0xAC,0x60,0x1F,0x53,0xB8,0xED,0x11},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0xAE,0x51,0xE5,0x62,0xE7,0x9A,0x99,0x39},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},{0x04,0x02,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x9B,0x21,0xE4,0x31,0x6A,0x00,0x29,0x62},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02},{0x06,0x04,0x02,0x08,0x01,0x03,0x05,0x07}},
	{{0x65,0x24,0x0C,0x41,0x4F,0xC2,0x21,0x93},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04},{0x0A,0x04,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x7F,0xEB,0xAE,0x93,0xE5,0x30,0x08,0xBD},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08},{0x12,0x04,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x49,0x7B,0x70,0x74,0x9B,0x35,0x1B,0x83},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10},{0x22,0x04,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x02,0x3C,0x15,0x6B,0xED,0xA5,0x64,0x6C},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20},{0x42,0x04,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0xE8,0x37,0xE0,0xE2,0xC6,0x45,0x24,0xF3},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40},{0x02,0x06,0x04,0x08,0x01,0x03,0x05,0x07}},
	{{0xAB,0xBD,0x30,0x05,0x29,0xC8,0xF7,0x12},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80},{0x02,0x08,0x06,0x04,0x01,0x03,0x05,0x07}},
	{{0x17,0xE8,0x97,0xF0,0x99,0xB6,0x79,0x31},{0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00},{0x02,0x0C,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x49,0xA4,0xF0,0x8F,0x5F,0x96,0x83,0x16},{0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00},{0x02,0x14,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x60,0xF5,0x7E,0x54,0xAA,0x41,0x83,0xD4},{0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00},{0x02,0x24,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x1D,0xF6,0x3B,0x6B,0x85,0x55,0xF0,0x4B},{0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00},{0x02,0x44,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x1F,0xDC,0x95,0x1A,0xEA,0x6B,0x4B,0xB4},{0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00},{0x02,0x04,0x08,0x06,0x01,0x03,0x05,0x07}},
	{{0xEC,0x93,0x72,0xF0,0x3B,0xA9,0xF5,0x0B},{0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00},{0x02,0x04,0x0A,0x08,0x01,0x03,0x05,0x07}},
	{{0xDE,0x57,0x5C,0xBE,0x2D,0x55,0x03,0x12},{0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00},{0x02,0x04,0x0E,0x08,0x01,0x03,0x05,0x07}},
	{{0x1E,0xD2,0xB5,0xCE,0x90,0xC9,0xC1,0xCC},{0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00},{0x02,0x04,0x16,0x08,0x01,0x03,0x05,0x07}},
	{{0xD8,0x65,0x96,0x4E,0xE7,0x74,0x99,0xB8},{0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00},{0x02,0x04,0x26,0x08,0x01,0x03,0x05,0x07}},
	{{0xE3,0x7A,0x29,0x83,0x31,0xD5,0x3A,0x54},{0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00},{0x02,0x04,0x46,0x08,0x01,0x03,0x05,0x07}},
	{{0x3A,0xB5,0x1A,0x34,0x34,0x25,0x12,0xF0},{0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00},{0x02,0x04,0x06,0x0A,0x01,0x03,0x05,0x07}},
	{{0xF2,0x88,0xEE,0x6F,0x70,0x6F,0xC2,0x52},{0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00},{0x02,0x04,0x06,0x0C,0x01,0x03,0x05,0x07}},
	{{0x76,0xEF,0xEB,0x80,0x52,0x43,0x83,0x57},{0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00},{0x02,0x04,0x06,0x10,0x01,0x03,0x05,0x07}},
	{{0x1C,0x09,0x8E,0x3B,0x23,0x23,0x52,0xB5},{0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00},{0x02,0x04,0x06,0x18,0x01,0x03,0x05,0x07}},
	{{0xA9,0x13,0xA2,0xBE,0xCF,0x1A,0xC4,0x9A},{0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00},{0x02,0x04,0x06,0x28,0x01,0x03,0x05,0x07}},
	{{0x25,0x56,0x4B,0xB0,0xC8,0x2A,0xD4,0x27},{0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x00},{0x02,0x04,0x06,0x48,0x01,0x03,0x05,0x07}},
	{{0xB1,0x04,0x57,0x3F,0xA7,0x16,0x62,0xD4},{0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x03,0x01,0x05,0x07}},
	{{0x45,0x46,0xED,0xCC,0xE7,0xD3,0x8E,0xA3},{0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x05,0x03,0x01,0x07}},
	{{0x22,0x6D,0xB5,0x35,0xE0,0x5A,0xE0,0x90},{0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x09,0x03,0x05,0x07}},
	{{0xB8,0xF5,0xE5,0x44,0xC5,0x98,0x4A,0xBD},{0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x11,0x03,0x05,0x07}},
	{{0xAC,0x78,0x0A,0x23,0x9E,0xF6,0xBC,0xA0},{0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x21,0x03,0x05,0x07}},
	{{0x46,0x6B,0x2D,0x70,0x41,0x17,0xBF,0x3D},{0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x41,0x03,0x05,0x07}},
	{{0x64,0x44,0x24,0x71,0xA2,0x56,0xDF,0xB5},{0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x05,0x03,0x07}},
	{{0xC4,0x00,0x52,0x24,0xA2,0xD6,0x16,0x7A},{0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x07,0x05,0x03}},
	{{0xD8,0x4A,0x80,0x1E,0x95,0x5B,0x70,0xC4},{0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x0B,0x05,0x07}},
	{{0x08,0x56,0x6E,0xB5,0x64,0xD6,0x47,0x4E},{0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x13,0x05,0x07}},
	{{0x41,0x6F,0xBA,0xA4,0xEB,0xAE,0xA0,0x55},{0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x23,0x05,0x07}},
	{{0x62,0x9D,0xDE,0x72,0x84,0x4A,0x53,0xD5},{0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x43,0x05,0x07}},
	{{0x39,0xD3,0x2B,0x66,0xB8,0x08,0x40,0x2E},{0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x07,0x05}},
	{{0xAF,0x67,0xA9,0x18,0x57,0x21,0xAF,0x8D},{0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x09,0x07}},
	{{0x34,0xBC,0x9D,0xBC,0xC4,0xC2,0x3B,0xC8},{0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x0D,0x07}},
	{{0xB6,0x50,0xF9,0x81,0xF6,0xBF,0x90,0x3C},{0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x15,0x07}},
	{{0x71,0x41,0x93,0xA1,0x59,0x81,0xA5,0x52},{0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x25,0x07}},
	{{0x6B,0x00,0xBD,0x74,0x1C,0x3C,0xE0,0x1A},{0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x45,0x07}},
	{{0x76,0xFD,0x0B,0xD0,0x41,0xD2,0x82,0x5D},{0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0x09}},
	{{0xC6,0x3A,0x1C,0x25,0x63,0x5A,0x2F,0x0E},{0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0x0B}},
	{{0xD9,0x0E,0xD7,0x30,0xE2,0xAD,0xA9,0x87},{0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0x0F}},
	{{0x6B,0x81,0xC6,0xD1,0x05,0x09,0x87,0x1E},{0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0x17}},
	{{0xB4,0xA7,0x1E,0x02,0x54,0x37,0x43,0x35},{0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0x27}},
	{{0x45,0x14,0x7C,0x7F,0xE0,0xDE,0x09,0x65},{0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0x47}},
	{{0x78,0xB0,0xF5,0x20,0x8B,0x7D,0xF3,0xDD},{0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00},{0xFE,0x04,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x88,0xB3,0x3C,0xE1,0xF7,0x87,0x42,0xA1},{0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0xFC,0x06,0x08,0x01,0x03,0x05,0x07}},
	{{0x11,0x2F,0xB2,0xF7,0xE2,0xB2,0x4F,0x6E},{0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0xFA,0x08,0x01,0x03,0x05,0x07}},
	{{0x25,0x56,0x4E,0xC6,0xEB,0x2D,0x74,0x5B},{0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0xF8,0x01,0x03,0x05,0x07}},
	{{0x7E,0x98,0x37,0xF9,0x80,0x8F,0x09,0x82},{0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0xFF,0x03,0x05,0x07}},
	{{0xF9,0xB5,0x62,0x3B,0xD8,0x7B,0x3C,0x3F},{0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0xFD,0x05,0x07}},
	{{0x29,0xC5,0x2B,0xFA,0xD1,0xFC,0x5C,0xC7},{0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0xFB,0x07}},
	{{0xC1,0xA3,0x09,0x71,0xBD,0x8E,0xAF,0x2F},{0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x06,0x08,0x01,0x03,0x05,0xF9}},
	{{0xB6,0xDD,0xD1,0xAD,0xAA,0x15,0x6F,0x29},{0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x01,0x03,0x05,0x02,0x07,0x04,0x06,0x08}},
	{{0x65,0x34,0x03,0x19,0x17,0xB3,0xA3,0x96},{0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x01,0x06,0x08,0x03,0x05,0x07}},
	{{0xF9,0x38,0x43,0x56,0x52,0xE5,0xB1,0xA9},{0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x01,0x02,0x04,0x06,0x08,0x03,0x05,0x07}},

	{{0xA4,0xA0,0xAF,0xDA,0x48,0xB0,0xA1,0x10},{0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x01,0x02,0x04,0x06,0x03,0x08,0x05,0x07}},
	{{0x55,0x15,0x8A,0x0D,0x48,0x29,0x01,0xD8},{0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x02,0x04,0x01,0x06,0x03,0x05,0x08,0x07}},
	{{0xC4,0x81,0x96,0x7D,0xA3,0xB7,0x73,0x50},{0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x01,0x02,0x03,0x05,0x04,0x06,0x08,0x07}},
	{{0x36,0x73,0xDF,0xC1,0x1B,0x98,0xA8,0x1D},{0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x01,0x02,0x03,0x04,0x05,0x06,0x08,0x07}},
	{{0xCE,0xE0,0xB3,0x1B,0x41,0xEB,0x15,0x12},{0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x01,0x02,0x03,0x04,0x06,0x05,0x08,0x07}},
	{{0},{0},{0}}
};


int testKeyDiversificationWithMasterkeyTestcases()
{

	int error = 0;
	int i;

	uint8_t empty[8]={0};
	prnlog("[+} Testing encryption/decryption");

	for (i = 0;  memcmp(testcases+i,empty,8) ; i++) {
		error += testDES(testcases[i],ctx_enc, ctx_dec);
	}
	if(error)
	{
		prnlog("[+] %d errors occurred (%d testcases)", error, i);
	}else
	{
		prnlog("[+] Hashing seems to work (%d testcases)", i);
	}
	return error;
}


void print64bits(char*name, uint64_t val)
{
	printf("%s%08x%08x\n",name,(uint32_t) (val >> 32) ,(uint32_t) (val & 0xFFFFFFFF));
}

uint64_t testCryptedCSN(uint64_t crypted_csn, uint64_t expected)
{
	int retval = 0;
	uint8_t result[8] = {0};
	if(debug_print) prnlog("debug_print %d", debug_print);
	if(debug_print) print64bits("    {csn}      ", crypted_csn );

	uint64_t crypted_csn_swapped = swapZvalues(crypted_csn);

	if(debug_print) print64bits("    {csn-revz} ", crypted_csn_swapped);

	hash0(crypted_csn, result);
    uint64_t resultbyte = x_bytes_to_num(result,8 );
	if(debug_print) print64bits("    hash0      " , resultbyte );

	if(resultbyte != expected )
	{

		if(debug_print) {
			prnlog("\n[+] FAIL!");
			print64bits("    expected       " ,  expected );
		}
		retval = 1;

	}else
	{
		if(debug_print) prnlog(" [OK]");
	}
	return retval;
}

int testDES2(uint64_t csn, uint64_t expected)
{
	uint8_t result[8] = {0};
	uint8_t input[8] = {0};

	print64bits("   csn ", csn);
    x_num_to_bytes(csn, 8,input);

	des_crypt_ecb(&ctx_enc,input, result);

    uint64_t crypt_csn = x_bytes_to_num(result, 8);
	print64bits("   {csn}    ", crypt_csn );
	print64bits("   expected ", expected );

	if( expected == crypt_csn )
	{
		prnlog("[+] OK");
		return 0;
	}else
	{
		return 1;
	}
}

/**
 * These testcases come from http://www.proxmark.org/forum/viewtopic.php?pid=10977#p10977
 * @brief doTestsWithKnownInputs
 * @return
 */
int doTestsWithKnownInputs()
{

	// KSel from http://www.proxmark.org/forum/viewtopic.php?pid=10977#p10977
	int errors = 0;
	prnlog("[+] Testing DES encryption");
//	uint8_t key[8] = {0x6c,0x8d,0x44,0xf9,0x2a,0x2d,0x01,0xbf};
	prnlog("[+] Testing foo");
	uint8_t key[8] = {0x6c,0x8d,0x44,0xf9,0x2a,0x2d,0x01,0xbf};

	des_setkey_enc( &ctx_enc, key);
	testDES2(0xbbbbaaaabbbbeeee,0xd6ad3ca619659e6b);

	prnlog("[+] Testing hashing algorithm");

	errors += testCryptedCSN(0x0102030405060708,0x0bdd6512073c460a);
	errors += testCryptedCSN(0x1020304050607080,0x0208211405f3381f);
	errors += testCryptedCSN(0x1122334455667788,0x2bee256d40ac1f3a);
	errors += testCryptedCSN(0xabcdabcdabcdabcd,0xa91c9ec66f7da592);
	errors += testCryptedCSN(0xbcdabcdabcdabcda,0x79ca5796a474e19b);
	errors += testCryptedCSN(0xcdabcdabcdabcdab,0xa8901b9f7ec76da4);
	errors += testCryptedCSN(0xdabcdabcdabcdabc,0x357aa8e0979a5b8d);
	errors += testCryptedCSN(0x21ba6565071f9299,0x34e80f88d5cf39ea);
	errors += testCryptedCSN(0x14e2adfc5bb7e134,0x6ac90c6508bd9ea3);

	if(errors)
	{
		prnlog("[+] %d errors occurred (9 testcases)", errors);
	}else
	{
		prnlog("[+] Hashing seems to work (9 testcases)" );
	}
	return errors;
}

int readKeyFile(uint8_t key[8])
{

	FILE *f;

	f = fopen("iclass_key.bin", "rb");
	if (f)
	{
		if(fread(key, sizeof(key), 1, f) == 1) return 0;
	}
	return 1;

}


int doKeyTests(uint8_t debuglevel)
{
	debug_print = debuglevel;

	prnlog("[+] Checking if the master key is present (iclass_key.bin)...");
	uint8_t key[8] = {0};
	if(readKeyFile(key))
	{
		prnlog("[+] Master key not present, will not be able to do all testcases");
	}else
	{

		//Test if it's the right key...
		uint8_t i;
		uint8_t j = 0;
		for(i =0 ; i < sizeof(key) ; i++)
			j += key[i];

		if(j != 185)
		{
			prnlog("[+] A key was loaded, but it does not seem to be the correct one. Aborting these tests");
		}else
		{
			prnlog("[+] Key present");

			prnlog("[+] Checking key parity...");
			des_checkParity(key);
			des_setkey_enc( &ctx_enc, key);
			des_setkey_dec( &ctx_dec, key);
			// Test hashing functions
			prnlog("[+] The following tests require the correct 8-byte master key");
			testKeyDiversificationWithMasterkeyTestcases();
		}
	}
	prnlog("[+] Testing key diversification with non-sensitive keys...");
	doTestsWithKnownInputs();
	return 0;
}

/**

void checkParity2(uint8_t* key)
{

	uint8_t stored_parity = key[7];
	printf("Parity byte: 0x%02x\n", stored_parity);
	int i;
	int byte;
	int fails =0;
	BitstreamIn bits = {key, 56, 0};

	bool parity = 0;

	for(i =0 ; i  < 56; i++)
	{

		if ( i > 0 && i % 7 == 0)
		{
			parity = !parity;
			bool pbit = stored_parity & (0x80 >> (byte));
			if(parity != pbit)
			{
				printf("parity2 fail byte %d, should be %d, was %d\n", (i / 7), parity, pbit);
				fails++;
			}
			parity =0 ;
			byte = i / 7;
		}
		parity = parity ^ headBit(&bits);
	}
	if(fails)
	{
		printf("parity2 fails: %d\n", fails);
	}else
	{
		printf("Key syntax is with parity bits grouped in the last byte!\n");
	}
}
void modifyKey_put_parity_last(uint8_t * key, uint8_t* output)
{
	uint8_t paritybits = 0;
	bool parity =0;
	BitstreamOut out = { output, 0,0};
	unsigned int bbyte, bbit;
	for(bbyte=0; bbyte <8 ; bbyte++ )
	{
		for(bbit =0 ; bbit< 7 ; bbit++)
		{
			bool bit = *(key+bbyte) & (1 << (7-bbit));
			pushBit(&out,bit);
			parity ^= bit;
		}
		bool paritybit = *(key+bbyte) & 1;
		paritybits |= paritybit << (7-bbyte);
		parity = 0;

	}
	output[7] = paritybits;
	printf("Parity byte: %02x\n", paritybits);
}

 * @brief Modifies a key with parity bits last, so that it is formed with parity
 *		bits inside each byte
 * @param key
 * @param output

void modifyKey_put_parity_allover(uint8_t * key, uint8_t* output)
{
	bool parity =0;
	BitstreamOut out = { output, 0,0};
	BitstreamIn in = {key, 0,0};
	unsigned int bbyte, bbit;
	for(bbit =0 ; bbit < 56 ; bbit++)
	{

		if( bbit > 0 && bbit % 7 == 0)
		{
			pushBit(&out,!parity);
			parity = 0;
		}
		bool bit = headBit(&in);
		pushBit(&out,bit );
		parity ^= bit;

	}
	pushBit(&out, !parity);


	if(	des_key_check_key_parity(output))
	{
		printf("modifyKey_put_parity_allover fail, DES key invalid parity!");
	}

}

*/


