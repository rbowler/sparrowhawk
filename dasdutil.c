/* DASDUTIL.C   (c) Copyright Roger Bowler, 1999                     */
/*              Hercules DASD Utilities: Common subroutines          */

/*-------------------------------------------------------------------*/
/* This module contains common subroutines used by DASD utilities    */
/*-------------------------------------------------------------------*/

#include "hercules.h"
#include "dasdblks.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define SPACE           ((BYTE)' ')

/*-------------------------------------------------------------------*/
/* ASCII to EBCDIC translate tables                                  */
/*-------------------------------------------------------------------*/
unsigned char
ascii_to_ebcdic[] = {
"\x00\x01\x02\x03\x37\x2D\x2E\x2F\x16\x05\x25\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\x3C\x3D\x32\x26\x18\x19\x1A\x27\x22\x1D\x35\x1F"
"\x40\x5A\x7F\x7B\x5B\x6C\x50\x7D\x4D\x5D\x5C\x4E\x6B\x60\x4B\x61"
"\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\x7A\x5E\x4C\x7E\x6E\x6F"
"\x7C\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xD1\xD2\xD3\xD4\xD5\xD6"
"\xD7\xD8\xD9\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xAD\xE0\xBD\x5F\x6D"
"\x79\x81\x82\x83\x84\x85\x86\x87\x88\x89\x91\x92\x93\x94\x95\x96"
"\x97\x98\x99\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xC0\x6A\xD0\xA1\x07"
"\x68\xDC\x51\x42\x43\x44\x47\x48\x52\x53\x54\x57\x56\x58\x63\x67"
"\x71\x9C\x9E\xCB\xCC\xCD\xDB\xDD\xDF\xEC\xFC\xB0\xB1\xB2\xB3\xB4"
"\x45\x55\xCE\xDE\x49\x69\x04\x06\xAB\x08\xBA\xB8\xB7\xAA\x8A\x8B"
"\x09\x0A\x14\xBB\x15\xB5\xB6\x17\x1B\xB9\x1C\x1E\xBC\x20\xBE\xBF"
"\x21\x23\x24\x28\x29\x2A\x2B\x2C\x30\x31\xCA\x33\x34\x36\x38\xCF"
"\x39\x3A\x3B\x3E\x41\x46\x4A\x4F\x59\x62\xDA\x64\x65\x66\x70\x72"
"\x73\xE1\x74\x75\x76\x77\x78\x80\x8C\x8D\x8E\xEB\x8F\xED\xEE\xEF"
"\x90\x9A\x9B\x9D\x9F\xA0\xAC\xAE\xAF\xFD\xFE\xFB\x3F\xEA\xFA\xFF"
        };

unsigned char
ebcdic_to_ascii[] = {
"\x00\x01\x02\x03\xA6\x09\xA7\x7F\xA9\xB0\xB1\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\xB2\xB4\x08\xB7\x18\x19\x1A\xB8\xBA\x1D\xBB\x1F"
"\xBD\xC0\x1C\xC1\xC2\x0A\x17\x1B\xC3\xC4\xC5\xC6\xC7\x05\x06\x07"
"\xC8\xC9\x16\xCB\xCC\x1E\xCD\x04\xCE\xD0\xD1\xD2\x14\x15\xD3\xFC"
"\x20\xD4\x83\x84\x85\xA0\xD5\x86\x87\xA4\xD6\x2E\x3C\x28\x2B\xD7"
"\x26\x82\x88\x89\x8A\xA1\x8C\x8B\x8D\xD8\x21\x24\x2A\x29\x3B\x5E"
"\x2D\x2F\xD9\x8E\xDB\xDC\xDD\x8F\x80\xA5\x7C\x2C\x25\x5F\x3E\x3F"
"\xDE\x90\xDF\xE0\xE2\xE3\xE4\xE5\xE6\x60\x3A\x23\x40\x27\x3D\x22"
"\xE7\x61\x62\x63\x64\x65\x66\x67\x68\x69\xAE\xAF\xE8\xE9\xEA\xEC"
"\xF0\x6A\x6B\x6C\x6D\x6E\x6F\x70\x71\x72\xF1\xF2\x91\xF3\x92\xF4"
"\xF5\x7E\x73\x74\x75\x76\x77\x78\x79\x7A\xAD\xA8\xF6\x5B\xF7\xF8"
"\x9B\x9C\x9D\x9E\x9F\xB5\xB6\xAC\xAB\xB9\xAA\xB3\xBC\x5D\xBE\xBF"
"\x7B\x41\x42\x43\x44\x45\x46\x47\x48\x49\xCA\x93\x94\x95\xA2\xCF"
"\x7D\x4A\x4B\x4C\x4D\x4E\x4F\x50\x51\x52\xDA\x96\x81\x97\xA3\x98"
"\x5C\xE1\x53\x54\x55\x56\x57\x58\x59\x5A\xFD\xEB\x99\xED\xEE\xEF"
"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\xFE\xFB\x9A\xF9\xFA\xFF"
        };

/*-------------------------------------------------------------------*/
/* Subroutine to convert a null-terminated string to upper case      */
/*-------------------------------------------------------------------*/
void string_to_upper (BYTE *source)
{
int     i;                              /* Array subscript           */

    for (i = 0; source[i] != '\0'; i++)
        source[i] = toupper(source[i]);

} /* end function string_to_upper */

/*-------------------------------------------------------------------*/
/* Subroutine to convert a null-terminated string to lower case      */
/*-------------------------------------------------------------------*/
void string_to_lower (BYTE *source)
{
int     i;                              /* Array subscript           */

    for (i = 0; source[i] != '\0'; i++)
        source[i] = tolower(source[i]);

} /* end function string_to_lower */

/*-------------------------------------------------------------------*/
/* Subroutine to convert a string to EBCDIC and pad with blanks      */
/*-------------------------------------------------------------------*/
void convert_to_ebcdic (BYTE *dest, int len, BYTE *source)
{
int     i;                              /* Array subscript           */

    for (i = 0; i < len && source[i] != '\0'; i++)
        dest[i] = ascii_to_ebcdic[source[i]];

    while (i < len)
        dest[i++] = 0x40;

} /* end function convert_to_ebcdic */

/*-------------------------------------------------------------------*/
/* Subroutine to convert an EBCDIC string to an ASCIIZ string.       */
/* Removes trailing blanks and adds a terminating null.              */
/* Returns the length of the ASCII string excluding terminating null */
/*-------------------------------------------------------------------*/
int make_asciiz (BYTE *dest, int destlen, BYTE *src, int srclen)
{
int             len;                    /* Result length             */

    for (len=0; len < srclen && len < destlen-1; len++)
        dest[len] = ebcdic_to_ascii[src[len]];
    while (len > 0 && dest[len-1] == SPACE) len--;
    dest[len] = '\0';

    return len;

} /* end function make_asciiz */

/*-------------------------------------------------------------------*/
/* Subroutine to print a data block in hex and character format.     */
/*-------------------------------------------------------------------*/
void data_dump ( void *addr, int len )
{
unsigned int    maxlen = 2048;
unsigned int    i, xi, offset, startoff = 0;
BYTE            c;
BYTE           *pchar;
BYTE            print_chars[17];
BYTE            hex_chars[64];
BYTE            prev_hex[64] = "";
int             firstsame = 0;
int             lastsame = 0;

    pchar = (unsigned char*)addr;

    for (offset=0; ; )
    {
        if (offset >= maxlen && offset <= len - maxlen)
        {
            offset += 16;
            pchar += 16;
            prev_hex[0] = '\0';
            continue;
        }
        if ( offset > 0 )
        {
            if ( strcmp ( hex_chars, prev_hex ) == 0 )
            {
                if ( firstsame == 0 ) firstsame = startoff;
                lastsame = startoff;
            }
            else
            {
                if ( firstsame != 0 )
                {
                    if ( lastsame == firstsame )
                        printf ("Line %4.4X same as above\n",
                                firstsame );
                    else
                        printf ("Lines %4.4X to %4.4X same as above\n",
                                firstsame, lastsame );
                    firstsame = lastsame = 0;
                }
                printf ("+%4.4X %s %s\n",
                        startoff, hex_chars, print_chars);
                strcpy ( prev_hex, hex_chars );
            }
        }

        if ( offset >= len ) break;

        memset ( print_chars, 0, sizeof(print_chars) );
        memset ( hex_chars, SPACE, sizeof(hex_chars) );
        startoff = offset;
        for (xi=0, i=0; i < 16; i++)
        {
            c = *pchar++;
            if (offset < len) {
                sprintf(hex_chars+xi, "%2.2X", c);
                print_chars[i] = '.';
                if (isprint(c)) print_chars[i] = c;
                c = ebcdic_to_ascii[c];
                if (isprint(c)) print_chars[i] = c;
            }
            offset++;
            xi += 2;
            hex_chars[xi] = SPACE;
            if ((offset & 3) == 0) xi++;
        } /* end for(i) */
        hex_chars[xi] = '\0';

    } /* end for(offset) */

} /* end function data_dump */

