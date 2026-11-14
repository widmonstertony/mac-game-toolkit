#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL %d\n",__LINE__);return 1;}}while(0)
static const WCHAR title[]=L"游戏崩溃";
static const WCHAR content[]=L"发生了意外错误。<a href=\"https://example.com/\">帮助</a> FHE11";
static const WCHAR close_text[]=L"退出";
static int title_ok,content_ok,button_ok, cjk_ok;
static void to_bytes(const WCHAR *in,WCHAR *out,int mode){
    char buf[512];int size=WideCharToMultiByte(CP_UTF8,0,in,-1,buf,sizeof(buf),0,0);
    static const WCHAR windows1252[32] = {
        0x20ac,0x81,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0x8d,0x017d,0x8f,
        0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0x9d,0x017e,0x0178};
    for(int i=0;i<size;i++){ BYTE ch=buf[i];out[i]=mode==1252&&ch>=0x80&&ch<=0x9f?windows1252[ch-0x80]:ch; }
}
static BOOL CALLBACK check_child(HWND window,LPARAM unused){
    (void)unused;
    WCHAR cls[64], text[512];GetClassNameW(window,cls,64);GetWindowTextW(window,text,512);
    if(!lstrcmpW(cls,L"SysLink"))content_ok=!lstrcmpW(text,content);
    if(GetDlgCtrlID(window)==IDOK)button_ok=!lstrcmpW(text,close_text);
    HFONT font=(HFONT)SendMessageW(window,WM_GETFONT,0,0);
    if(font&&GetDlgCtrlID(window)==IDOK){
        HDC dc=GetDC(window);HGDIOBJ old=SelectObject(dc,font);WORD glyph[2]={0xffff,0xffff};
        if(GetGlyphIndicesW(dc,close_text,2,glyph,GGI_MARK_NONEXISTING_GLYPHS)!=GDI_ERROR)cjk_ok=glyph[0]!=0xffff&&glyph[1]!=0xffff;
        SelectObject(dc,old);ReleaseDC(window,dc);
    }
    return TRUE;
}
static HRESULT CALLBACK callback(HWND window,UINT msg,WPARAM wp,LPARAM lp,LONG_PTR data){
    (void)wp; (void)lp; (void)data;
    if(msg==TDN_CREATED){WCHAR text[64];GetWindowTextW(window,text,64);title_ok=!lstrcmpW(text,title);
        EnumChildWindows(window,check_child,0);PostMessageW(window,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),(LPARAM)GetDlgItem(window,IDOK));}
    return S_OK;
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0); CHECK(argc==3);BOOL baseline=!strcmp(argv[2],"baseline");
    CHECK(LoadLibraryA(argv[1]));
    for(int i=0;i<3;i++){
        WCHAR t[512], body[512], btn[512];int mode=i==1?1252:0;
        to_bytes(title,t,mode);to_bytes(content,body,mode);to_bytes(close_text,btn,mode);
        TASKDIALOG_BUTTON button={IDOK,i==2?close_text:btn};
        TASKDIALOGCONFIG cfg={0};cfg.cbSize=sizeof(cfg);cfg.dwFlags=TDF_ENABLE_HYPERLINKS;cfg.pszWindowTitle=i==2?title:t;
        cfg.pszContent=i==2?content:body;cfg.cButtons=1;cfg.pButtons=&button;cfg.pfCallback=callback;
        title_ok=content_ok=button_ok=cjk_ok=0;int id=0;
        CHECK(SUCCEEDED(TaskDialogIndirect(&cfg,&id,0,0))&&id==IDOK);
        printf("case=%d title=%d body=%d button=%d glyphs=%d\n",i,title_ok,content_ok,button_ok,cjk_ok);
        if(!baseline||i==2) CHECK(title_ok&&content_ok&&button_ok&&cjk_ok);
        else CHECK(!title_ok&&!content_ok&&!button_ok);
    }
    return 0;
}
