// implementation of generic tools

#include "tools.h"
#include <new>

extern "C" {
#include <jpeglib.h>
}

//////////////////////////// pool ///////////////////////////

pool::pool()
{
    blocks = 0;
    allocnext(POOLSIZE);
    for(int i = 0; i<MAXBUCKETS; i++) reuse[i] = NULL;
};

void *pool::alloc(size_t size)
{
    if(size>MAXREUSESIZE)
    {
        return malloc(size);
    }
    else
    {
        size = bucket(size);
        void **r = (void **)reuse[size];
        if(r)
        {
            reuse[size] = *r;
            return (void *)r;
        }
        else
        {
            size <<= PTRBITS;
            if(left<size) allocnext(POOLSIZE);
            char *r = p;
            p += size;
            left -= size;
            return r;
        };
    };
};

void pool::dealloc(void *p, size_t size)
{
    if(size>MAXREUSESIZE)
    {
        free(p);
    }
    else
    {
        size = bucket(size);
        if(size)    // only needed for 0-size free, are there any?
        {
            *((void **)p) = reuse[size];
            reuse[size] = p;
        };
    };
};

void *pool::realloc(void *p, size_t oldsize, size_t newsize)
{
    void *np = alloc(newsize);
    if(!oldsize) return np;
    memcpy(np, p, newsize>oldsize ? oldsize : newsize);
    dealloc(p, oldsize);
    return np;
};

void pool::dealloc_block(void *b)
{
    if(b)
    {
        dealloc_block(*((char **)b));
        free(b);
    };
}

void pool::allocnext(size_t allocsize)
{
    char *b = (char *)malloc(allocsize+PTRSIZE);
    *((char **)b) = blocks;
    blocks = b;
    p = b+PTRSIZE;
    left = allocsize;
};

char *pool::string(const char *s, size_t l)
{
    char *b = (char *)alloc(l+1);
    strncpy(b,s,l);
    b[l] = 0;
    return b;  
};

pool *gp()  // useful for global buffers that need to be initialisation order independant
{
    static pool *p = NULL;
    return p ? p : (p = new pool());
};


///////////////////////// misc tools ///////////////////////
char *path(char *s)
{
    for(char *t = s; (t = strpbrk(t, "/\\")); *t++ = PATHDIV);
    return s;
};

char *loadfile(char *fn, int *size)
{
    FILE *f = fopen(fn, "rb");
    if(!f) return NULL;
    fseek(f, 0, SEEK_END);
    int len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len+1);
    if(!buf) return NULL;
    buf[len] = 0;
    size_t rlen = fread(buf, 1, len, f);
    fclose(f);
    if(len!=rlen || len<=0) 
    {
        free(buf);
        return NULL;
    };
    if(size!=NULL) *size = len;
    return buf;
};

bool fileexists(char *fn)
{
    FILE *f = fopen(fn, "rb");
    if(!f) return false;
	fclose(f);
	return true;
}

void endianswap(void *memory, int stride, int length)   // little indians as storage format
{
    if(*((char *)&stride)) return;
    loop(w, length) loop(i, stride/2)
    {
        uchar *p = (uchar *)memory+w*stride;
        uchar t = p[i];
        p[i] = p[stride-i-1];
        p[stride-i-1] = t;
    };
}

// read jpeg as 24 bpp pixel array; client must free(pixels)
bool readjpeg( char * fname, int &xs, int &ys, void *& pixels )
{
	FILE * input_file = fopen( fname, "rb" );
	if ( !input_file ) 
		return false;

	jpeg_decompress_struct cinfo;
	jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, input_file);
	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);

	xs = cinfo.output_width;
	ys = cinfo.output_height;

	pixels = malloc( xs * ys * 3 );

	JSAMPROW scanlines[1];
	scanlines[0] = (JSAMPROW) pixels;
	while ( cinfo.output_scanline < cinfo.output_height )
	{
		jpeg_read_scanlines(&cinfo, scanlines, 1);
		scanlines[0] += xs * 3;
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	fclose( input_file );

	return true;
}
