struct fb_fix_screeninfo {
    char           id[16];
    unsigned long  smem_start;
    unsigned int   smem_len;
    unsigned int   type;
    unsigned int   type_aux;
    unsigned int   visual;
    unsigned short xpanstep;
    unsigned short ypanstep;
    unsigned short ywrapstep;
    unsigned int   line_length;
    unsigned long  mmio_start;
    unsigned int   mmio_len;
    unsigned int   accel;
    unsigned short capabilities;
    unsigned short reserved[2];
};
#define MYOFF(f) ((unsigned long)&((struct fb_fix_screeninfo*)0)->f)
