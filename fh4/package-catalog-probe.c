/* Query real Wine package APIs, without overriding them or launching FH4. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <appmodel.h>
#include <stdio.h>
#include <stdlib.h>
int wmain(int argc,wchar_t **argv) {
    if(argc!=4)return 2;
    UINT32 n=0;LONG r=PackageIdFromFullName(argv[1],0,&n,NULL);
    if(r!=ERROR_INSUFFICIENT_BUFFER)return 3;
    PACKAGE_ID *id=malloc(n);if(!id)return 4;
    if(PackageIdFromFullName(argv[1],0,&n,(BYTE*)id))return 5;
    UINT32 len=0;r=GetPackagePath(id,0,&len,NULL);
    if(r!=ERROR_INSUFFICIENT_BUFFER){printf("PACKAGE_PATH_FAIL code=%ld\n",r);return 6;}
    WCHAR *path=malloc(len*sizeof(WCHAR));if(!path)return 7;
    if(GetPackagePath(id,0,&len,path)||_wcsicmp(path,argv[2]))return 8;
    UINT32 count=0,chars=0;
    r=GetPackagesByPackageFamily(argv[3],&count,NULL,&chars,NULL);
    if(r!=ERROR_INSUFFICIENT_BUFFER||count<1){printf("PACKAGE_FAMILY_FAIL code=%ld count=%u\n",r,count);return 9;}
    WCHAR **names=calloc(count,sizeof(*names)),*buffer=calloc(chars,sizeof(WCHAR));
    if(!names||!buffer)return 10;
    if(GetPackagesByPackageFamily(argv[3],&count,names,&chars,buffer))return 11;
    UINT32 found=0;for(UINT32 i=0;i<count;i++)if(!wcscmp(names[i],argv[1]))found++;
    if(found!=1)return 12;
    printf("PACKAGE_CATALOG_PASS real_lookup=true exact_path=true family_members=%u exact_package_matches=%u\n",count,found);
    free(buffer);free(names);free(path);free(id);return 0;
}
