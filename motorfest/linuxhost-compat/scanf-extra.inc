/* SPDX-License-Identifier: AGPL-3.0-only
 * Included after the variadic adapter and System V argument reader.
 * Uses MinGW's scanner after translating Linux long-sized output pointers. */
SYSV int lx_vsscanf_extra(const char *input,const char *fmt,struct lx_va_list *ap)
{
    size_t length=strlen(fmt);
    if(length>(SIZE_MAX-1)/3)return format_error(75);
    char *translated=malloc(length*3+1),*out=translated;
    void **arguments=calloc(length+1,sizeof(void*));
    if(!translated || !arguments){free(translated);free(arguments);return format_error(12);}
    const char *p=fmt;size_t count=0;int result=-1;
    while(*p)
    {
        if(*p!='%'){*out++=*p++;continue;}
        *out++=*p++;
        if(*p=='%'){*out++=*p++;continue;}
        int suppressed=*p=='*';if(suppressed)*out++=*p++;
        while(*p>='0' && *p<='9')*out++=*p++;
        enum { NONE,HH,H,L,LL,J,Z,T,LD } modifier=NONE;
        if(*p=='h'){++p;modifier=H;if(*p=='h'){++p;modifier=HH;}}
        else if(*p=='l'){++p;modifier=L;if(*p=='l'){++p;modifier=LL;}}
        else if(*p=='j'){++p;modifier=J;}
        else if(*p=='z'){++p;modifier=Z;}
        else if(*p=='t'){++p;modifier=T;}
        else if(*p=='L'){++p;modifier=LD;}
        char c=*p++;
        if(!c)goto invalid;
        int integer=strchr("diuoxXn",c)!=NULL,floating=strchr("aAeEfFgG",c)!=NULL;
        if(integer)
        {
            if(modifier==LD)goto invalid;
            if(modifier>=L){*out++='l';*out++='l';}
            else if(modifier==H || modifier==HH){*out++='h';if(modifier==HH)*out++='h';}
        }
        else if(floating)
        {
            if(modifier==L)*out++='l';else if(modifier==LD)*out++='L';
            else if(modifier!=NONE)goto invalid;
        }
        else if(!strchr("sc[p",c) || modifier!=NONE)goto invalid;
        *out++=c;
        if(c=='[')
        {
            if(*p=='^')*out++=*p++;
            if(*p==']')*out++=*p++;
            while(*p && *p!=']')*out++=*p++;
            if(*p!=']')goto invalid;
            *out++=*p++;
        }
        if(!suppressed)arguments[count++]=(void*)next_gp(ap);
    }
    *out=0;
    /* All scanf varargs are pointers; Win64 va_list is contiguous pointer slots. */
    result=__mingw_vsscanf(input,translated,(va_list)arguments);
    goto done;
invalid:
    result=format_error(22); /* unsupported positional, allocation or wide forms */
done:
    free(arguments);free(translated);return result;
}
VARIADIC(lx_sscanf_extra,lx_vsscanf_extra,2,"%rdx");
extern void lx_sscanf_extra(void);
