/*
|| ----------------------------------------------------------------------------
||
|| HETLIB.C     (c) Copyright Leland Lucius, 2000-2002
||              Released under terms of the Q Public License.
||
|| Library for managing Hercules Emulated Tapes.  
||
|| ----------------------------------------------------------------------------
*/

#include "hetlib.h"
#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <zlib.h>
#if defined( HET_BZIP2 )
#include <bzlib.h>
#endif /* defined( HET_BZIP2 ) */

#undef HETDEBUGR
#undef HETDEBUGW

/*
|| Local constant data
*/
static const char *het_errstr[] = 
{
    "No error",
    "File error",
    "Tapemark read",
    "Beginning of tape",
    "End of tape",
    "BOR not found",
    "EOR not found",
    "Unexpected tapemark",
    "Buffer not big enough",
    "Premature EOF",
    "Decompression error",
    "Unknown compression method",
    "Compression error",
    "Specified length to big",
    "Write protected",
    "Bad function code passed",
    "Bad compression method",
    "Bad compression level",
    "Bad write chunk size",
    "Invalid direction specified",
    "Insufficient memory",
    "Couldn't read block header",
    "Inconsistent compression flags",
    "Invalid error code",
};
#define HET_ERRSTR_MAX ( sizeof( het_errstr) / sizeof( het_errstr[ 0 ] ) )

/*==DOC==

    NAME
            het_open - Open an HET format file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_open( HETB **hetb, char *filename, int flags )
        
    DESCRIPTION
            The het_open() function opens the file indicated by the "filename"
            parameter and, if successful, places the address of an HETB at
            the location pointed to by the "hetb" parameter.
            
            Currently, "HETOPEN_CREATE" is the only flag available and has
            the same function as the O_CREAT flag of the open(3) function.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0
            and the address of the newly allocated HETB will be place at the
            "hetb" location.
            
            If an error occurs, then the return value will be < 0 and will be
            one of the following:
            
            HETE_NOMEM          Insufficient memory to allocate an HETB
            
            HETE_ERROR          File system error - check errno(3)

            For other possible errors, see:
                het_read_header()
                het_tapemark()
                het_rewind()

    NOTES
            Even if het_open() fails, you should still call het_close().

    EXAMPLE
            //
            // Create an NL tape
            //

            #include "hetlib.h"
            
            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], HETOPEN_CREATE );
                if( rc < 0 )
                {
                    printf( "het_open() returned: %d\n", rc );
                }
                else
                {
                    printf( "%s successfully created\n", argv[ 1 ] );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_read_header(), het_tapemark(), het_rewind(), het_close()

==DOC==*/

int 
het_open( HETB **hetb, char *filename, int flags )
{
    HETB *thetb;
	char *omode;
    int rc;
    int fd;

    /*
    || Initialize
    */
    *hetb = NULL;
    
    /*
    || Allocate a new HETB
    */
    thetb = calloc( 1, sizeof( HETB ) );
    if( thetb == NULL )
    {
        return( HETE_NOMEM );
    }

    /*
    || Set defaults
    */
    thetb->compress   = HETDFLT_COMPRESS;
    thetb->decompress = HETDFLT_DECOMPRESS;
    thetb->method     = HETDFLT_METHOD;
    thetb->level      = HETDFLT_LEVEL;
    thetb->chksize    = HETDFLT_CHKSIZE;

    /*
    || Translate HET create flag to filesystem flag
    */
    flags = ( ( flags & HETOPEN_CREATE ) ? O_CREAT : 0 );

    /*
    || Open the tape file
    */
    omode = "r+b";
    fd = open( filename, O_RDWR | flags, S_IRUSR | S_IWUSR | S_IRGRP );
    if( fd == -1 && ( errno == EROFS || errno == EACCES ) )
    {
        /*
        || Retry open if file resides on readonly file system
        */
        omode = "rb";
        thetb->writeprotect = TRUE;
        fd = open( filename, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP );
    }

    /*
    || Error out if both opens failed
    */
    if( fd == -1 )
    {
        free( thetb );
        return( HETE_ERROR );
    }

    /*
    || Associate stream with file descriptor
    */
    thetb->fd = fdopen( fd, omode );
    if( thetb->fd == NULL )
    {
        rc = errno;
        close( fd );
        errno = rc;
        free( thetb );
        return( HETE_ERROR );
    }

    /*
    || If uninitialized tape, write 2 tapemarks to make it a valid NL tape
    */
    rc = het_read_header( thetb );
    if( rc < 0 && rc != HETE_TAPEMARK )
    {
        if( rc != HETE_EOT )
        {
            return( rc );
        }

        rc = het_tapemark( thetb );
        if( rc < 0 )
        {
            return( rc );
        }

        rc = het_tapemark( thetb );
        if( rc < 0 )
        {
            return( rc );
        }
    }

    /*
    || Reposition tape to load point
    */
    rc = het_rewind( thetb );
    if( rc < 0 )
    {
        return( rc );
    }

    /*
    || Give the caller the new HETB
    */
    *hetb = thetb;

    return( 0 );

}

/*==DOC==

    NAME
            het_close - Close an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_close( HETB **hetb )
        
    DESCRIPTION
            The het_close() function closes an HET file and releases the
            HETB.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0
            and the location specified by the "hetb" parameter will be set
            to NULL.
            
            If an error occurs, then the return value will be < 0.  At this
            time, no errors will be returned.

    EXAMPLE
            //
            // Create an NL tape
            //

            #include "hetlib.h"
            
            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], HETOPEN_CREATE );
                if( rc < 0 )
                {
                    printf( "het_open() returned: %d\n", rc );
                }
                else
                {
                    printf( "%s successfully created\n", argv[ 1 ] );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open()

==DOC==*/

int 
het_close( HETB **hetb )
{
    
    /*
    || Only free the HETB if we have one
    */
    if( *(hetb) != NULL )
    {
        /*
        || Only close the file if opened
        */
        if( (*hetb)->fd != NULL )
        {
            fclose( (*hetb)->fd );
        }
        free( *(hetb) );
    }

    /*
    || Reinitialize pointer
    */
    *hetb = NULL;

    return( 0 );
}

/*==DOC==

    NAME
            het_cntl - Control HET file behavior
        
    SYNOPSIS
            #include "hetlib.h"

            int het_cntl( HETB *hetb, int func, unsigned long val )
        
    DESCRIPTION
            The het_cntl() function allows you to get/set several values
            that control how an HET file behaves.  The value of the "val"
            parameter depends on the function code.

            The possible modes are:
            
            HETCNTL_GET         Should be ORed (|) with the function codes
                                to retrieve the current setting.  (Default)
                            
            HETCNTL_SET         Should be ORed (|) with the function codes
                                to set a new value.
                            
            The possible function codes are:
            
            HETCNTL_COMPRESS    val=TRUE to enable write compression (see notes)
                                Values:     FALSE (disable)
                                            TRUE (enable)
                                Default:    HETDFLT_COMPRESS (TRUE)

            HETCNTL_DECOMPRESS  val=TRUE to enable read decompression
                                Values:     FALSE (diable)
                                            TRUE (enable)
                                Default:    HETDFLT_DECOMPRESS (TRUE)
            
            HETCNTL_METHOD      val=Compression method to use
                                Values:     HETMETH_ZLIB (1)
                                            HETMETH_BZLIB (2)
                                Default:    HETDFLT_METHOD (HETMETH_ZLIB)
            
            HETCNTL_LEVEL       val=Level of compression
                                Min:        HETMIN_LEVEL (1)
                                Max:        HETMAX_LEVEL (9)
                                Default:    HETDFLT_LEVEL (4)
                                
            HETCNTL_CHUNKSIZE   val=Size of output chunks (see notes)
                                Min:        HETMIN_CHUNKSIZE (4096)
                                Max:        HETMAX_CHUNKSIZE (65535)
                                Default:    HETDFLT_CHUNKSIZE (65535)
            
    RETURN VALUE
            If no errors are detected then the return value will be either
            the current setting for a "get" request or >= 0 for a "set"
            request.

            If an error occurs, then the return value will be < 0 and can be
            one of the following:

            HETE_BADMETHOD      Specified method out of range

            HETE_BADLEVEL       Specified level out of range

            HETE_BADCHUNKSIZE   Specified chunk size out of range

            HETE_BADFUNC        Unrecognized function code

    NOTES
            Each block on an HET file is made up of "HETCNTL_CHUNKSIZE" sized
            chunks.  There can be 1 or many chunks per block.

            If you wish to create an AWSTAPE compatible file, specify a chunk
            size of 4096 and disable write compression.

    EXAMPLE
            //
            // Create an NL tape and write an uncompressed string to it
            //

            #include "hetlib.h"

            char data[] = "This is a test";
            
            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], HETOPEN_CREATE );
                if( rc >= 0 )
                {
                    rc = het_cntl( hetb, HETCNTL_SET | HETCNTL_COMPRESS, FALSE );
                    if( rc >= 0 )
                    {
                        rc = het_write( hetb, data, sizeof( data ) );
                        if( rc >= 0 )
                        {
                            printf( "Data successfully written\n" );
                        }
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_cntl(), het_write(), het_close()

==DOC==*/

int 
het_cntl( HETB *hetb, int func, unsigned long val )
{
    int mode;
    
    /*
    || Isolate the mode
    */
    mode = func & HETCNTL_SET;

    /*
    || Process the requested function
    */
    switch( func & ( ~( HETCNTL_GET | HETCNTL_SET ) ) )
    {
        case HETCNTL_COMPRESS:
            if( mode == HETCNTL_GET )
            {
                return( hetb->compress );
            }

            hetb->compress = ( val ? TRUE : FALSE );
        break;
        
        case HETCNTL_DECOMPRESS:
            if( mode == HETCNTL_GET )
            {
                return( hetb->decompress );
            }

            hetb->decompress = ( val ? TRUE : FALSE );
        break;
        
        case HETCNTL_METHOD:
            if( mode == HETCNTL_GET )
            {
                return( hetb->method );
            }

            if( val < HETMIN_METHOD || val > HETMAX_METHOD )
            {
                return( HETE_BADMETHOD );
            }

            hetb->method = val;
        break;

        case HETCNTL_LEVEL:
            if( mode == HETCNTL_GET )
            {
                return( hetb->level );
            }

            if( val < HETMIN_LEVEL || val > HETMAX_LEVEL )
            {
                return( HETE_BADLEVEL );
            }

            hetb->level = val;
        break;
        
        case HETCNTL_CHUNKSIZE:
            if( mode == HETCNTL_GET )
            {
                return( hetb->chksize );
            }

            if( val < HETMIN_CHUNKSIZE || val > HETMAX_CHUNKSIZE )
            {
                return( HETE_BADSIZE );
            }

            hetb->chksize = val;
        break;
        
        default:
            return( HETE_BADFUNC );
        break;
    }

    /*
    || Success
    */

    return( 0 );
}

/*==DOC==

    NAME
            het_read_header - Retrieve the next chunk header from an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_read_header( HETB *hetb )
        
    DESCRIPTION
            Retrieves the next chunk header and stores it in the HETB.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0
            and the current block count will be incremented.

            If an error occurs, then the return value will be < 0 and can be
            one of the following:

            HETE_EOT            End of tape encountered

            HETE_ERROR          File system error - check errno(3)

            HETE_TAPEMARK       Tape mark encountered

    NOTES
            This function is not normally called from user programs and its
            behavior may change.

    EXAMPLE
            //
            // Read a chunk header from an HET file.
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_read_header( hetb );
                    if( rc >= 0 )
                    {
                        printf( "Header read:\n" );
                        printf( "  Current length:  %d\n", HETHDR_CLEN( hetb ) );
                        printf( "  Previous length: %d\n", HETHDR_PLEN( hetb ) );
                        printf( "  Flags1:          %x\n", hetb->flags1 );
                        printf( "  Flags2:          %x\n", hetb->flags2 );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_close()

==DOC==*/

int
het_read_header( HETB *hetb )
{
    int rc;

    /*
    || Read in a headers worth of data
    */
    rc = fread( &hetb->chdr, sizeof( HETHDR ), 1, hetb->fd );
    if( rc != 1 )
    {
        /*
        || Return EOT if at end of physical file
        */
        if( feof( hetb->fd ) )
        {
            return( HETE_EOT );
        }

        /*
        || Something else must've happened
        */
        return( HETE_ERROR );
    }

#if defined( HETDEBUGR )
    printf("read hdr: pl=%d, cl=%d, f1=%02x, f2=%02x\n",
        HETHDR_PLEN( hetb ), HETHDR_CLEN( hetb ),
        hetb->chdr.flags1, hetb->chdr.flags2);
#endif

    /*
    || Bump block number if done with entire block
    */
    if( hetb->chdr.flags1 & ( HETHDR_FLAGS1_EOR | HETHDR_FLAGS1_TAPEMARK ) )
    {
        hetb->cblk++;
    }

    /*
    || Check for tape marks
    */
    if( hetb->chdr.flags1 & HETHDR_FLAGS1_TAPEMARK )
    {
        return( HETE_TAPEMARK );
    }

    /*
    || Success
    */
    return( 0 );
}

/*==DOC==

    NAME
            het_read - Retrieve the next block from an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_read( HETB *hetb,  void *sbuf )
        
    DESCRIPTION
            Read the next block of data into the "sbuf" memory location.  The
            length of "sbuf" should be at least HETMAX_BLOCKSIZE bytes.
            
    RETURN VALUE
            If no errors are detected then the return value will be the
            size of the block read.  This will be either the compressed or
            uncompressed length depending on the current AWSCNTL_DECOMPRESS
            setting.

            If an error occurs, then the return value will be < 0 and can be
            one of the following:

            HETE_ERROR          File system error - check errno(3)

            HETE_BADBOR         Beginning of record expected but not found
            
            HETE_BADCOMPRESS    Compression mismatch between related chunks
            
            HETE_OVERFLOW       Record too large for buffer
            
            HETE_PREMEOF        Premature EOF on file
            
            HETE_DECERR         Decompression error (stored in errno(3))

            HETE_UNKMETH        Unknown compression method encountered

            For other possible errors, see:
                het_read_header()

    EXAMPLE
            //
            // Read a block from an HET file.
            //

            #include "hetlib.h"

            char buffer[ HETMAX_BLOCKSIZE ];

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_read( hetb, buffer );
                    if( rc >= 0 )
                    {
                        printf( "Block read - length: %d\n", rc );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_read_header(), het_close()

==DOC==*/

int
het_read( HETB *hetb, void *sbuf )
{
    char *tptr;
    int rc;
    unsigned long slen;
    int flags;
    unsigned long tlen;
    char tbuf[ HETMAX_BLOCKSIZE ];

    /*
    || Initialize
    */
    flags = 0;
    tlen = 0;
    tptr = sbuf;

    /*
    || Read chunks until entire block has been read
    */
    do
    {
        /*
        || Get a header
        */
        rc = het_read_header( hetb );
        if( rc < 0 )
        {
            return( rc );
        }

        /*
        || Have we seen a BOR chunk yet?
        */
        if( !( flags & HETHDR_FLAGS1_BOR ) )
        {
            /*
            || Nope, so this chunk MUST have the BOR set
            */
            if( !( hetb->chdr.flags1 & HETHDR_FLAGS1_BOR ) )
            {
                return( HETE_BADBOR );
            }

            /*
            || If block is compressed (and decompression is desired), set
            || destination pointer.
            */
            if( hetb->decompress )
            {
                if( hetb->chdr.flags1 & HETHDR_FLAGS1_COMPRESS )
                {
                    tptr = tbuf;
                }
            }

            /*
            || Save flags for later validation
            */
            flags = hetb->chdr.flags1;
        }
        else
        {
            /*
            || Yep, so this chunk MUST NOT have the BOR flag set
            */
            if( hetb->chdr.flags1 & HETHDR_FLAGS1_BOR )
            {
                return( HETE_BADBOR );
            }
        }

        /*
        || Compression flags from related chunks must match
        */
        if( ( flags & HETHDR_FLAGS1_COMPRESS ) !=
            ( hetb->chdr.flags1 & HETHDR_FLAGS1_COMPRESS ) )
        {
            return( HETE_BADCOMPRESS );
        }

        /*
        || Calculate running length
        */
        slen = HETHDR_CLEN( hetb );
        tlen += slen;

        /*
        || Can't be bigger than HETMAX_BLOCKSIZE
        */
        if( tlen > HETMAX_BLOCKSIZE )
        {
            return( HETE_OVERFLOW );
        }

        /*
        || Finally read in the chunk data
        */
        rc = fread( tptr, 1, slen, hetb->fd );
        if( rc != slen )
        {
            if( feof( hetb->fd ) )
            {
                return( HETE_PREMEOF );
            }

            return( HETE_ERROR );
        }

        /*
        || Bump destination pointer to next possible location
        */
        tptr += slen;
    }
    while( !( hetb->chdr.flags1 & HETHDR_FLAGS1_EOR ) );

    /*
    || Save compressed length (means cblksize size and ublksize will be the
    || same for uncompressed data)
    */
    hetb->cblksize = tlen;

    /*
    || Decompress data if requested
    */
    if( hetb->decompress )
    {
        switch( hetb->chdr.flags1 & HETHDR_FLAGS1_COMPRESS )
        {
            case 0:
            break;

            case HETHDR_FLAGS1_ZLIB:
                slen = HETMAX_BLOCKSIZE;
    
                rc = uncompress( sbuf, &slen, tbuf, tlen );
                if( rc != Z_OK )
                {
                    errno = rc;
                    return( HETE_DECERR );
                }
    
                tlen = slen;
            break;
    
#if defined( HET_BZIP2 )
            case HETHDR_FLAGS1_BZLIB:
                slen = HETMAX_BLOCKSIZE;
    
                rc = BZ2_bzBuffToBuffDecompress( sbuf,
                                                 (unsigned int *) &slen,
                                                 tbuf,
                                                 tlen,
                                                 0,
                                                 0 );
                if (rc != BZ_OK)
                {
                    errno = rc;
                    return( HETE_DECERR );
                }
    
                tlen = slen;
            break;
#endif /* defined( HET_BZIP2 ) */
    
            default:
                return( HETE_UNKMETH );
            break;
        }
    }

    /*
    || Save uncompressed length
    */
    hetb->ublksize = tlen;

    /*
    || Success
    */
    return( tlen );
}

/*==DOC==

    NAME
            het_write_header - Write a chunk header to an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_write_header( HETB *hetb, int len, int flags1, int flags2 )
        
    DESCRIPTION
            Constructs and writes a chunk header to an HET file.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0
            and the current block count will be incremented.

            If an error occurs, then the return value will be < 0 and can be
            one of the following:

            HETE_BADLEN         "len" parameter out of range

            HETE_PROTECTED      File is write protected

            HETE_ERROR          File system error - check errno(3)

    NOTES
            This function is not normally called from user programs and its
            behavior may change.

            If this function is called and the tape is not positioned at the
            end, then the file will be truncated to the current position.  This
            means that rewriting blocks is not currently possible.

    EXAMPLE
            //
            // Write a chunk header to an HET file.
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_write_header( hetb, 0, HETHDR_TAPEMARK, 0 );
                    if( rc >= 0 )
                    {
                        printf( "Header written\n" );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_close()

==DOC==*/

int
het_write_header( HETB *hetb, int len, int flags1, int flags2 )
{
    int rc;

#if defined( HETDEBUGW )
    printf("write hdr: pl=%d, cl=%d, f1=%02x, f2=%02x\n",
        HETHDR_PLEN( hetb ), HETHDR_CLEN( hetb ),
        hetb->chdr.flags1, hetb->chdr.flags2);
#endif

    /*
    || Validate length
    */
    if( len > HETMAX_CHUNKSIZE )
    {
        return( HETE_BADLEN );
    }

    /*
    || Can't write anything on readonly media
    */
    if( hetb->writeprotect )
    {
        return( HETE_PROTECTED );
    }

    /*
    || For tapemarks, length must be zero.
    */
    if( flags1 & HETHDR_FLAGS1_TAPEMARK )
    {
        len = 0;
    }

    /*
    || According to Linux fopen() man page, a positioning function is required
    || between reads and writes.  Is this REALLY necessary???
    */
    if( !hetb->readlast )
    {
        fseek( hetb->fd, 0, SEEK_CUR );
        hetb->readlast = FALSE;
    }
        
    /*
    || If this is the first write, truncate the file
    */
    if( !hetb->truncated )
    {
        rc = ftell( hetb->fd );
        if( rc == -1 )
        {
            return( HETE_ERROR );
        }
        
        rc = ftruncate( fileno( hetb->fd ), rc );
        if( rc == -1 )
        {
            return( HETE_ERROR );
        }
        
        hetb->truncated = TRUE;
    }
        
    /*
    || Construct the header
    */
    hetb->chdr.plen[ 0 ] = hetb->chdr.clen[ 0 ];
    hetb->chdr.plen[ 1 ] = hetb->chdr.clen[ 1 ];
    hetb->chdr.clen[ 0 ] = len & 0xFF;
    hetb->chdr.clen[ 1 ] = ( len >> 8 ) & 0xFF;
    hetb->chdr.flags1    = flags1;
    hetb->chdr.flags2    = flags2;

    /*
    || Write it out
    */
    rc = fwrite( &hetb->chdr, sizeof( HETHDR ), 1, hetb->fd );
    if( rc != 1 )
    {
        return( HETE_ERROR );
    }

    /*
    || Bump block count if done with entire block
    */
    if( hetb->chdr.flags1 & ( HETHDR_FLAGS1_EOR | HETHDR_FLAGS1_TAPEMARK ) )
    {
        hetb->cblk++;
    }

    /*
    || Success
    */
    return 0;
}

/*==DOC==

    NAME
            het_write - Write a block to an HET file

    SYNOPSIS
            #include "hetlib.h"

            int het_write( HETB *hetb, void *sbuf, int slen )

    DESCRIPTION
            Writes a block of data specified by "sbuf" with a length of "slen"
            to an HET file.  Depending on the current HETCNTL_COMPRESS setting,
            the data may be compressed prior to writing.
            
    RETURN VALUE
            If no errors are detected then the return value will be the
            size of the block written.  This will be either the compressed or
            uncompressed length depending on the current AWSCNTL_COMPRESS
            setting.

            If an error occurs, then the return value will be < 0 and can be
            one of the following:

            HETE_ERROR          File system error - check errno(3)

            HETE_BADLEN         "slen" parameter out of range
            
            HETE_BADCOMPRESS    Compression mismatch between related chunks
            
            For other possible errors, see:
                het_write_header()

    EXAMPLE
            //
            // Create an NL tape and write a string to it
            //

            #include "hetlib.h"

            char data[] = "This is a test";
            
            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], HETOPEN_CREATE );
                if( rc >= 0 )
                {
                    rc = het_write( hetb, data, sizeof( data ) );
                    if( rc >= 0 )
                    {
                        printf( "Block written - length: %d\n", rc );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_write_header(), het_close()

==DOC==*/

int
het_write( HETB *hetb, void *sbuf, int slen )
{
    int rc;
    int flags;
    unsigned long tlen;
    char tbuf[ HETMAX_BLOCKSIZE ];

    /*
    || Validate
    */
    if( slen > HETMAX_BLOCKSIZE )
    {
        return( HETE_BADLEN );
    }

    /*
    || Initialize
    */
    flags = HETHDR_FLAGS1_BOR;

    /*
    || Save uncompressed length
    */
    hetb->ublksize = slen;

    /*
    || Compress data if requested
    */
    if( hetb->compress )
    {
        switch( hetb->method )
        {
            case HETHDR_FLAGS1_ZLIB:
                tlen = sizeof( tbuf );

                rc = compress2( tbuf, &tlen, sbuf, slen, hetb->level );
                if( rc != Z_OK )
                {
                    errno = rc;
                    return( HETE_COMPERR );
                }

                if( tlen < slen )
                {
                    sbuf = tbuf;
                    slen = tlen;
                    flags |= HETHDR_FLAGS1_ZLIB;
                }
            break;

#if defined( HET_BZIP2 )
            case HETHDR_FLAGS1_BZLIB:
                tlen = sizeof( tbuf );

                rc = BZ2_bzBuffToBuffCompress( tbuf,
                                               (unsigned int *) &tlen,
                                               sbuf,
                                               slen,
                                               hetb->level,
                                               0,
                                               0 );
                if( rc != BZ_OK )
                {
                    errno = rc;
                    return( HETE_COMPERR );
                }

                if( tlen < slen )
                {
                    sbuf = tbuf;
                    slen = tlen;
                    flags |= HETHDR_FLAGS1_BZLIB;
                }
            break;
#endif /* defined( HET_BZIP2 ) */
        }
    }

    /*
    || Save compressed length
    */
    hetb->cblksize = slen;

    /*
    || Write block, breaking it into "chksize" chunks
    */
    do
    {
        /*
        || Last chunk for this block?
        */
        if( slen <= hetb->chksize )
        {
            flags |= HETHDR_FLAGS1_EOR;
            tlen = slen;
        }
        else
        {
            tlen = hetb->chksize;
        }

        /*
        || Write the header
        */
        rc = het_write_header( hetb, tlen, flags, 0 );
        if( rc < 0 )
        {
            return( rc );
        }

        /*
        || Write the block
        */
        rc = fwrite( sbuf, 1, tlen, hetb->fd );
        if( rc != tlen )
        {
            return( HETE_ERROR );
        }
        
        /*
        || Bump pointers and turn off BOR flag
        */
        sbuf += tlen;
        slen -= tlen;
        flags &= (~HETHDR_FLAGS1_BOR);
    }
    while( slen > 0 );

    /*
    || Success
    */
    return( hetb->cblksize );
}

/*==DOC==

    NAME
            het_tapemark - Write a tape mark to an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_tapemark( HETB *hetb )
        
    DESCRIPTION
            Writes a special chunk header to an HET file to simulate a tape
            mark.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0.

            If an error occurs, then the return value will be < 0 and will be
            the same as those returned by het_write_header().

    EXAMPLE
            //
            // Write a tapemark to an HET file
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], HETOPEN_CREATE );
                if( rc >= 0 )
                {
                    rc = het_tapemark( hetb );
                    if( rc >= 0 )
                    {
                        printf( "Tape mark written\n" );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_write_header(), het_close()

==DOC==*/

int
het_tapemark( HETB *hetb )
{
    int rc;

    /*
    || Just write a tapemark header
    */
    rc = het_write_header( hetb, 0, HETHDR_FLAGS1_TAPEMARK, 0 );
    if( rc < 0 )
    {
        return( rc );
    }

    /*
    || Success
    */
    return( 0 );
}

/*==DOC==

    NAME
            het_locate - Locate a block within an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_locate( HETB *hetb, int block )
        
    DESCRIPTION
            Repositions the HET file to the start of the block specified by
            the "block" parameter.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0 and
            represents the new current block number.

            If an error occurs, then the return value will be < 0 and will
            the same as those returned by:
                het_rewind()
                het_fsb()

    NOTES
            Block numbers start at 0.

    EXAMPLE
            //
            // Seek to block #4 in HET file
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_locate( hetb, 4 );
                    if( rc >= 0 )
                    {
                        printf( "New tape position: %d\n", rc );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_rewind(), het_fsb(), het_close()

==DOC==*/

int
het_locate( HETB *hetb, int block )
{
    int rc;
    
    /*
    || Start the search from the beginning
    */
    rc = het_rewind( hetb );
    if( rc < 0 )
    {
        return( rc );
    }

    /*
    || Forward space until we reach the desired block
    */
    while( hetb->cblk < block )
    {
        rc = het_fsb( hetb );
        if( rc < 0 )
        {
            return( rc );
        }
    }

    return( hetb->cblk );
}
    
/*==DOC==

    NAME
            het_bsb - Backspace a block in an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_bsb( HETB *hetb )
        
    DESCRIPTION
            Repositions the current block pointer in an HET file to the 
            previous block.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0 and
            will be the new block number.

            If an error occurs, then the return value will be < 0 and can be
            one of the following:

            HETE_ERROR          File system error - check errno(3)

            HETE_BOT            Beginning of tape
            
            HETE_TAPEMARK       Tape mark encountered

            For other possible errors, see:
                het_rewind()
                het_read_header()

    EXAMPLE
            //
            // Backspace a block in an HET file
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_fsb( hetb );
                    if( rc >= 0 )
                    {
                        rc = het_bsb( hetb );
                        if( rc >= 0 )
                        {
                            printf( "New block number = %d\n", rc );
                        }
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_rewind(), het_read_header(), het_fsb(), het_close()

==DOC==*/

int
het_bsb( HETB *hetb )
{
    int rc;
    int newblk;
    int offset;
    int tapemark = FALSE;
    
    /*
    || Error if at BOT
    */
    if( hetb->cblk == 0 )
    {
        return( HETE_BOT );
    }

    /*
    || Get new block number
    */
    newblk = hetb->cblk - 1;

    /*
    || If new block is first on, then just rewind
    */
    if( newblk == 0 )
    {
        return( het_rewind( hetb ) );
    }

    /*
    || Calculate offset to get back to beginning of current block
    */
    offset = -( HETHDR_CLEN( hetb ) + sizeof( HETHDR ) );

    /*
    || Search backwards an entire block.  If the block is a tapemark, we can't
    || just return to caller since we must load the chunk header preceding it
    || to maintain the chdr in the HET.
    */
    do
    {
        /*
        || Reposition to start of chunk
        */
        rc = fseek( hetb->fd,
                    offset,
                    SEEK_CUR );
        if( rc == -1 )
        {
            return( HETE_ERROR );
        }

        /*
        || Read header, ignoring tapemarks
        */
        rc = het_read_header( hetb );
        if( rc < 0 && rc != HETE_TAPEMARK )
        {
            return( rc );
        }

        /*
        || Recalculate offset
        */
        offset = -( HETHDR_PLEN( hetb ) + ( sizeof( HETHDR ) * 2 ) );
    }
    while( hetb->chdr.flags1 & !( HETHDR_FLAGS1_BOR | HETHDR_FLAGS1_TAPEMARK ) );

    /*
    || Remember whether it's a tapemark or not
    */
    tapemark = ( hetb->chdr.flags1 & HETHDR_FLAGS1_TAPEMARK );

    /*
    || Reposition to chunk header preceding this one so we can load keep the
    || chdr in the HET current.
    */
    rc = fseek( hetb->fd,
                offset,
                SEEK_CUR );
    if( rc == -1 )
    {
        return( HETE_ERROR );
    }

    /*
    || Read header (ignore tapemarks)
    */
    rc = het_read_header( hetb );
    if( rc < 0 && ( rc != HETE_TAPEMARK ) )
    {
        return( rc );
    }

    /*
    || Finally reposition back to the where we should be
    */
    rc = fseek( hetb->fd,
                HETHDR_CLEN( hetb ),
                SEEK_CUR );
    if( rc == -1 )
    {
        return( HETE_ERROR );
    }

    /*
    || Store new block number
    */
    hetb->cblk = newblk;

    /*
    || Was it a tapemark?
    */
    if( tapemark )
    {
        return( HETE_TAPEMARK );
    }

    /*
    || Reset flag to force truncation if a write occurs
    */
    hetb->truncated = FALSE;

    /*
    || Return block number
    */
    return( hetb->cblk );
}
    
/*==DOC==

    NAME
            het_fsb - Foward space a block in an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_fsb( HETB *hetb )
        
    DESCRIPTION
            Repositions the current block pointer in an HET file to the 
            next block.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0 and
            will be the new block number.

            If an error occurs, then the return value will be < 0 and can be
            one of the following:

            HETE_ERROR          File system error - check errno(3)

            For other possible errors, see:
                het_read_header()

    EXAMPLE
            //
            // Forward space a block in an HET file
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_fsb( hetb );
                    if( rc >= 0 )
                    {
                        printf( "New block number = %d\n", rc );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_read_header(), het_close()

==DOC==*/

int
het_fsb( HETB *hetb )
{
    int rc;
    
    /*
    || Loop until we've processed an entire block
    */
    do
    {
        /*
        || Read header to get length of next chunk
        */
        rc = het_read_header( hetb );
        if( rc < 0 )
        {
            return( rc );
        }

        /*
        || Seek to next chunk
        */
        rc = fseek( hetb->fd,
                    HETHDR_CLEN( hetb ),
                    SEEK_CUR );
        if( rc == -1 )
        {
            return( HETE_ERROR );
        }
    }
    while( !( hetb->chdr.flags1 & HETHDR_FLAGS1_EOR ) );

    /*
    || Reset flag to force truncation if a write occurs
    */
    hetb->truncated = FALSE;

    /*
    || Return block number
    */
    return( hetb->cblk );
}
    
/*==DOC==

    NAME
            het_bsf - Backspace a file in an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_bsf( HETB *hetb )
        
    DESCRIPTION
            Repositions the current block pointer in an HET file to the 
            previous tapemark.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0 and
            will be the new block number.

            If an error occurs, then the return value will be < 0 and will be
            the same as those returned by het_bsb() with the exception that
            HETE_TAPEMARK and HETE_BOT will not occur.

    EXAMPLE
            //
            // Backspace a file in an HET file
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_fsf( hetb );
                    if( rc >= 0 )
                    {
                        rc = het_bsf( hetb );
                        if( rc >= 0 )
                        {
                            printf( "Backspaced (sort of :-))\n" );
                        }
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_bsb(), het_fsf(), het_close()

==DOC==*/

int
het_bsf( HETB *hetb )
{
    int rc;
    
    /*
    || Backspace until we hit a tapemark
    */
    do
    {
        rc = het_bsb( hetb );
    }
    while( rc >= 0 );
    
    /*
    || Success
    */
    if( ( rc == HETE_BOT ) || ( rc == HETE_TAPEMARK ) )
    {
        return( hetb->cblk );
    }

    /*
    || Failure
    */
    return( rc );
}
    
/*==DOC==

    NAME
            het_fsf - Forward space a file in an HET file
        
    SYNOPSIS
            #include "hetlib.h"

            int het_fsf( HETB *hetb )
        
    DESCRIPTION
            Repositions the current block pointer in an HET file to the 
            next tapemark.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0 and
            will be the new block number.

            If an error occurs, then the return value will be < 0 and will be
            the same as those returned by het_fsb() with the exception that
            HETE_TAPEMARK will not occur.

    EXAMPLE
            //
            // Forward space a file in an HET file
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_fsf( hetb );
                    if( rc >= 0 )
                    {
                        printf( "Forward spaced\n" );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_fsb(), het_close()

==DOC==*/

int
het_fsf( HETB *hetb )
{
    int rc;
    
    /*
    || Forward space until we hit a tapemark
    */
    do
    {
        rc = het_fsb( hetb );
    }
    while( rc >= 0 );
    
    /*
    || Success
    */
    if( rc == HETE_TAPEMARK )
    {
        return( hetb->cblk );
    }

    /*
    || Failure
    */
    return( rc );
}

/*==DOC==

    NAME
            het_rewind - Rewind an HET file 
        
    SYNOPSIS
            #include "hetlib.h"

            int het_rewind( HETB *hetb )
        
    DESCRIPTION
            Repositions the current block pointer in an HET file to the 
            load point.
            
    RETURN VALUE
            If no errors are detected then the return value will be >= 0 and
            represents the new block number (always 0).

            If an error occurs, then the return value will be < 0 and will be
            one of the following:
            
            HETE_ERROR          File system error - check errno(3)

    EXAMPLE
            //
            // Rewind an HET file to the load point.
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                HETB *hetb;
                int rc;

                rc = het_open( &hetb, argv[ 1 ], 0 );
                if( rc >= 0 )
                {
                    rc = het_rewind( hetb );
                    if( rc >= 0 )
                    {
                        printf( "Tape rewound\n" );
                    }
                }

                if( rc < 0 )
                {
                    printf( "HETLIB error: %d\n", rc );
                }

                het_close( &hetb );

                return( 0 );
            }

    SEE ALSO
            het_open(), het_close()

==DOC==*/

int
het_rewind( HETB *hetb )
{
    int rc;
    
    /*
    || Just seek to the beginning of the file
    */
    rc = fseek( hetb->fd,
                0,
                SEEK_SET );
    if( rc == -1 )
    {
        return( HETE_ERROR );
    }

    /*
    || Reset current block
    */
    hetb->cblk = 0;

    /*
    || Clear header for the heck of it
    */
    memset( &hetb->chdr, 0, sizeof( hetb->chdr ) );

    /*
    || Reset flag to force truncation if a write occurs
    */
    hetb->truncated = FALSE;

    /*
    || Return block number
    */
    return( hetb->cblk );
}

/*==DOC==

    NAME
            het_error - Returns a text message for an HET error code
        
    SYNOPSIS
            #include "hetlib.h"

            char *het_error( int rc )
        
    DESCRIPTION
            Simply returns a pointer to a string that describes the error
            code passed in the "rc" parameter.
            
    RETURN VALUE
            The return value is always valid and no errors are returned.

    EXAMPLE
            //
            // Print text of HETE_BADLEN.
            //

            #include "hetlib.h"

            int main( int argc, char *argv[] )
            {
                printf( "HETLIB error: %d = %s\n",
                    HETE_BADLEN,
                    het_error( HETE_BADLEN ) );

                return( 0 );
            }

    SEE ALSO

==DOC==*/

const char *
het_error( int rc )
{
    /*
    || If not an error just return the "OK" string
    */
    if( rc >= 0 )
    {
        rc = 0;
    }
    
    /*
    || Turn it into an index
    */
    rc = -rc;

    /*
    || Within range?
    */
    if( rc >= HET_ERRSTR_MAX )
    {
        rc = HET_ERRSTR_MAX - 1;
    }

    /*
    || Return string
    */
    return( het_errstr[ rc ] );
}
