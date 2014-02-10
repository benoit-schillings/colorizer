#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>



#include "bmp.c"

#define uchar unsigned char
#define ushort unsigned short
#define ulong  unsigned long

//---------------------------------------------------------------

typedef struct RgbColor
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
} RgbColor;

//---------------------------------------------------------------

typedef struct HsvColor
{
    unsigned char h;
    unsigned char s;
    unsigned char v;
} HsvColor;

//---------------------------------------------------------------

RgbColor HsvToRgb(HsvColor hsv)
{
    RgbColor rgb;
    unsigned char region, remainder, p, q, t;
    
    if (hsv.s == 0)
    {
        rgb.r = hsv.v;
        rgb.g = hsv.v;
        rgb.b = hsv.v;
        return rgb;
    }
    
    region = hsv.h / 43;
    remainder = (hsv.h - (region * 43)) * 6;
    
    p = (hsv.v * (255 - hsv.s)) >> 8;
    q = (hsv.v * (255 - ((hsv.s * remainder) >> 8))) >> 8;
    t = (hsv.v * (255 - ((hsv.s * (255 - remainder)) >> 8))) >> 8;
    
    switch (region)
    {
        case 0:
            rgb.r = hsv.v; rgb.g = t; rgb.b = p;
            break;
        case 1:
            rgb.r = q; rgb.g = hsv.v; rgb.b = p;
            break;
        case 2:
            rgb.r = p; rgb.g = hsv.v; rgb.b = t;
            break;
        case 3:
            rgb.r = p; rgb.g = q; rgb.b = hsv.v;
            break;
        case 4:
            rgb.r = t; rgb.g = p; rgb.b = hsv.v;
            break;
        default:
            rgb.r = hsv.v; rgb.g = p; rgb.b = q;
            break;
    }
    
    return rgb;
}

//---------------------------------------------------------------

HsvColor RgbToHsv(RgbColor rgb)
{
    HsvColor hsv;
    unsigned char rgbMin, rgbMax;
    
    rgbMin = rgb.r < rgb.g ? (rgb.r < rgb.b ? rgb.r : rgb.b) : (rgb.g < rgb.b ? rgb.g : rgb.b);
    rgbMax = rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b) : (rgb.g > rgb.b ? rgb.g : rgb.b);
    
    hsv.v = rgbMax;
    if (hsv.v == 0)
    {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }
    
    hsv.s = 255 * (long)(rgbMax - rgbMin) / hsv.v;
    if (hsv.s == 0)
    {
        hsv.h = 0;
        return hsv;
    }
    
    if (rgbMax == rgb.r)
        hsv.h = 0 + 43 * (rgb.g - rgb.b) / (rgbMax - rgbMin);
    else if (rgbMax == rgb.g)
        hsv.h = 85 + 43 * (rgb.b - rgb.r) / (rgbMax - rgbMin);
    else
        hsv.h = 171 + 43 * (rgb.r - rgb.g) / (rgbMax - rgbMin);
    
    return hsv;
}

//---------------------------------------------------------------

int read_input_bmp(char *filename, int xsize, int ysize, uchar *buffer)
{
    uchar *temp_buffer;
    
    //input is RGB, no alpha. so 24 bits per pixel
    
    temp_buffer = (uchar *)malloc(xsize * ysize * 3);

    FILE *fp_in = fopen(filename, "rb");

    if (fp_in == 0) {
        free((char *)temp_buffer);
        return -1;
    }
    
    fseek(fp_in, sizeof(struct BMPHeader), SEEK_SET);
    int cnt = fread(temp_buffer, 3, xsize * ysize, fp_in);

    fclose(fp_in);
    
    //flip vertical
    
    int x,y;
    
    for (y = 0; y < ysize; y++) {
        uchar   *src_ptr;
        uchar   *dst_ptr;
        
        src_ptr = temp_buffer + (y * xsize * 3);
        dst_ptr = buffer + (((ysize - 1) - y) * xsize * 4);
        
        for (x = 0; x < xsize; x++) {
            uchar   r,g,b;
            
            r = *src_ptr++;
            g = *src_ptr++;
            b = *src_ptr++;
            
            *dst_ptr++ = 0xff;      //alpha
            *dst_ptr++ = r;
            *dst_ptr++ = b;
            *dst_ptr++ = g;
        }
    }
    
    free((char *)temp_buffer);

    return cnt;
}

//---------------------------------------------------------------

#define	X_SIZE	640
#define Y_SIZE  1136


//---------------------------------------------------------------

void init_lookup_table(uchar *lookup, ulong color)
{
    // note that the lookup is done on 10 bits to avoid rounding errors

    int r, g, b;
    
    b = (color & 0xff);
    g = (color & 0xff00) >> 8;
    r = (color & 0xff0000) >> 16;
    
    RgbColor in_color;
    
    in_color.r = r;
    in_color.g = g;
    in_color.b = b;
    
    HsvColor out = RgbToHsv(in_color);
    
    out.s = 180;
    out.v = 255;
    
    in_color = HsvToRgb(out);
    
    
    
    for (int i = 0; i < 256 * 4; i++) {
        int brightness = i * (1.0 * 0.25);  //reduce the brightness
        brightness = brightness + 30;       //reduce the contrast.
        if (brightness > 255)
            brightness = 255;
        
        *(lookup + i * 4 + 0) = (brightness * in_color.r)/256;
        *(lookup + i * 4 + 1) = (brightness * in_color.g)/256;
        *(lookup + i * 4 + 2) = (brightness * in_color.b)/256;
    }
}

//---------------------------------------------------------------

uchar map_alpha(int cur_y, int ysize)
{
    int     ydiv3;
    
    ydiv3 = ysize / 3;
    
    if (cur_y < (ydiv3)) {
        return 127 + ((cur_y * 128) / ydiv3);
    }
    
    if (cur_y < (ydiv3 * 2)) {
        return 255;
    }
    
    return 255 - ((cur_y - ydiv3 * 2) * 128) / ydiv3;
}

//---------------------------------------------------------------

void convert_to_mono_and_adjust(uchar *buffer, uchar *lookup, int xsize, int ysize)
{
    int idx;
    int x;
    int y;
    
    idx = xsize * ysize;
    
    for (y = 0; y < ysize; y++) {
        uchar alpha;
        
        alpha = map_alpha(y, ysize);
        
        for ( x = 0; x < xsize; x++) {
        int r,g,b;
        
        //a = *(buffer + 0);      //not used
        r = *(buffer + 1);
        g = *(buffer + 2);
        b = *(buffer + 3);

        //Y=0.2126R+0.7152G+0.0722B.[5]
        
        //pretty good aproximation.
        
        ushort gray = (r+r+r) + ((g<<3) + g + g + g) + b;
        //divide by 16, but multiply by 4 given the lookup table
        //strcture
        
        gray = gray >> (4 - 2);
       
        // ARGB output
        
        *buffer++ = alpha;
        *buffer++ = lookup[(gray * 4)];
        *buffer++ = lookup[(gray * 4) + 1];
        *buffer++ = lookup[(gray * 4) + 2];
        }
    }
}

//---------------------------------------------------------------

int main()
{
    uchar   *input_buffer;
    
    

    input_buffer = (uchar *)malloc(X_SIZE * Y_SIZE * 4);
	int result = read_input_bmp("./pic.bmp", X_SIZE, Y_SIZE, input_buffer);
   if (result < 0) {
        printf("input file not found\n");
        return -1;
    }
    
    uchar   *lookup;
    
    lookup = (uchar *)malloc(256 * 4 * 4);
    init_lookup_table(lookup, 0xa4b6c7);

    convert_to_mono_and_adjust(input_buffer, lookup, X_SIZE, Y_SIZE);
    
    free((char*)lookup);
    
    write_bmp("result.bmp", X_SIZE, Y_SIZE, (char*)input_buffer);

}
