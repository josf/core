
/* 
   Copyright (C) Cfengine AS

   This file is part of Cfengine 3 - written and maintained by Cfengine AS.
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License  
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of Cfengine, the applicable Commerical Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.

*/

/*****************************************************************************/
/*                                                                           */
/* File: file_hashes.c                                                       */
/*                                                                           */
/*****************************************************************************/

#include "cf3.defs.h"
#include "cf3.extern.h"

/*******************************************************************/

int Hash(char *name)

{ int i, slot = 0;

 for (i = 0; name[i] != '\0'; i++)
   {
   slot = (CF_MACROALPHABET * slot + name[i]) % CF_HASHTABLESIZE;
   }

return slot;
}

/*******************************************************************/

int ElfHash(char *key)

{ unsigned int h = 0;
  unsigned int g;

while (*key)
  {
  h = (h << 4) + *key++;

  g = h & 0xF0000000;         /* Assumes int is 32 bit */

  if (g) 
     {
     h ^= g >> 24;
     }

  h &= ~g;
  }

return (h % CF_HASHTABLESIZE);
}

/*****************************************************************************/

int FileHashChanged(char *filename,unsigned char digest[EVP_MAX_MD_SIZE+1],int warnlevel,enum cfhashes type,struct Attributes attr,struct Promise *pp)

/* Returns false if filename never seen before, and adds a checksum
   to the database. Returns true if hashes do not match and also potentially
   updates database to the new value */

{ struct stat stat1, stat2;
  int i,needupdate = false, size = 21;
  unsigned char dbdigest[EVP_MAX_MD_SIZE+1],dbattr[EVP_MAX_MD_SIZE+1];
  CF_DB *dbp;
  FILE *fp;

Debug("HashChanged: key %s (type=%d) with data %s\n",filename,type,HashPrint(type,digest));

size = FileHashSize(type);

if (!OpenDB(HASHDB,&dbp))
   {
   cfPS(cf_error,CF_FAIL,"open",pp,attr,"Unable to open the hash database!");
   return false;
   }

if (ReadHash(dbp,type,filename,dbdigest))
   {
   for (i = 0; i < size; i++)
      {
      if (digest[i] != dbdigest[i])
         {
         Debug("Found cryptohash for %s in database but it didn't match\n",filename);
         
         if (EXCLAIM)
            {
            CfOut(warnlevel,"","!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            }
         
         CfOut(warnlevel,"","ALERT: Hash (%s) for %s changed!",FileHashName(type),filename);

         if (pp->ref)
            {
            CfOut(warnlevel,"","Preceding promise: %s",pp->ref);
            }
         
         if (EXCLAIM)
            {
            CfOut(warnlevel,"","!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            }
         
         if (attr.change.update)
            {
            CfOut(cf_verbose,""," -> Updating cryptohash for %s to %s\n",filename,HashPrint(type,digest));
            
            DeleteHash(dbp,type,filename);
            WriteHash(dbp,type,filename,digest);
            }
         
	 CloseDB(dbp);
         return true;                        /* Checksum updated but was changed */
         }
      }
   
   Debug("Found checksum for %s in database and it matched\n",filename);
   CloseDB(dbp);
   return false;
   }
else
   {
   /* Key was not found, so install it */
   cfPS(warnlevel,CF_CHG,"",pp,attr," !! File %s was not in %s database - new file found",filename,FileHashName(type));   
   Debug("Storing checksum for %s in database %s\n",filename,HashPrint(type,digest));
   WriteHash(dbp,type,filename,digest);
   
   CloseDB(dbp);
   return false;
   }
}

/*******************************************************************/

int CompareFileHashes(char *file1,char *file2,struct stat *sstat,struct stat *dstat,struct Attributes attr,struct Promise *pp)

{ static unsigned char digest1[EVP_MAX_MD_SIZE+1], digest2[EVP_MAX_MD_SIZE+1];
  int i;

Debug("CompareFileHashes(%s,%s)\n",file1,file2);

if (sstat->st_size != dstat->st_size)
   {
   Debug("File sizes differ, no need to compute checksum\n");
   return true;
   }
  
if (attr.copy.servers == NULL || strcmp(attr.copy.servers->item,"localhost") == 0)
   {
   HashFile(file1,digest1,cf_md5);
   HashFile(file2,digest2,cf_md5);

   for (i = 0; i < EVP_MAX_MD_SIZE; i++)
      {
      if (digest1[i] != digest2[i])
         {
         return true;
         }
      }

   Debug("Files were identical\n");
   return false;  /* only if files are identical */
   }
else
   {
   return CompareHashNet(file1,file2,attr,pp); /* client.c */
   }
}

/*******************************************************************/

int CompareBinaryFiles(char *file1,char *file2,struct stat *sstat,struct stat *dstat,struct Attributes attr,struct Promise *pp)

{ int fd1, fd2,bytes1,bytes2;
  char buff1[BUFSIZ],buff2[BUFSIZ];

Debug("CompareBinarySums(%s,%s)\n",file1,file2);

if (sstat->st_size != dstat->st_size)
   {
   Debug("File sizes differ, no need to compute checksum\n");
   return true;
   }
  
if (attr.copy.servers == NULL || strcmp(attr.copy.servers->item,"localhost") == 0)
   {
   fd1 = open(file1, O_RDONLY|O_BINARY, 0400);
   fd2 = open(file2, O_RDONLY|O_BINARY, 0400);
  
   do
      {
      bytes1 = read(fd1, buff1, BUFSIZ);
      bytes2 = read(fd2, buff2, BUFSIZ);

      if ((bytes1 != bytes2) || (memcmp(buff1, buff2, bytes1) != 0))
         {
         CfOut(cf_verbose,"","Binary Comparison mismatch...\n");
         close(fd2);
         close(fd1);
         return true;
         }
      }
   while (bytes1 > 0);
   
   close(fd2);
   close(fd1);
   
   return false;  /* only if files are identical */
   }
else
   {
   Debug("Using network md5 checksum instead\n");
   return CompareHashNet(file1,file2,attr,pp); /* client.c */
   }
}

/*******************************************************************/

void HashFile(char *filename,unsigned char digest[EVP_MAX_MD_SIZE+1],enum cfhashes type)

{ FILE *file;
  EVP_MD_CTX context;
  int len, md_len;
  unsigned char buffer[1024];
  const EVP_MD *md = NULL;

Debug2("HashFile(%d,%s)\n",type,filename);

if ((file = fopen(filename, "rb")) == NULL)
   {
   CfOut(cf_inform,"fopen","%s can't be opened\n", filename);
   }
else
   {
   md = EVP_get_digestbyname(FileHashName(type));
   
   EVP_DigestInit(&context,md);

   while (len = fread(buffer,1,1024,file))
      {
      EVP_DigestUpdate(&context,buffer,len);
      }

   EVP_DigestFinal(&context,digest,&md_len);
   
   /* Digest length stored in md_len */
   fclose (file);
   }
}

/*******************************************************************/

void HashList(struct Item *list,unsigned char digest[EVP_MAX_MD_SIZE+1],enum cfhashes type)

{ struct Item *ip;
  EVP_MD_CTX context;
  int md_len;
  const EVP_MD *md = NULL;

Debug2("HashList(%s)\n",FileHashName(type));

memset(digest,0,EVP_MAX_MD_SIZE+1);

md = EVP_get_digestbyname(FileHashName(type));

EVP_DigestInit(&context,md);

for (ip = list; ip != NULL; ip=ip->next) 
   {
   Debug(" digesting %s\n",ip->name);
   EVP_DigestUpdate(&context,ip->name,strlen(ip->name));
   }

EVP_DigestFinal(&context,digest,&md_len);
}

/*******************************************************************/

void HashString(char *buffer,int len,unsigned char digest[EVP_MAX_MD_SIZE+1],enum cfhashes type)

{ EVP_MD_CTX context;
  const EVP_MD *md = NULL;
  char *file_buffer;
  int md_len;

Debug2("HashString(%c)\n",type);

switch (type)
   {
   case cf_crypt:
       CfOut(cf_error,"","The crypt support is not presently implemented, please use md5 instead");
       break;
       
   default:
       md = EVP_get_digestbyname(FileHashName(type));

       if (md == NULL)
          {
          CfOut(cf_inform,""," !! Digest type %s not supported by OpenSSL library",CF_DIGEST_TYPES[type][0]);
          }
       
       EVP_DigestInit(&context,md); 
       EVP_DigestUpdate(&context,(unsigned char*)buffer,len);
       EVP_DigestFinal(&context,digest,&md_len);
       break;
   }
}

/*******************************************************************/

int HashesMatch(unsigned char digest1[EVP_MAX_MD_SIZE+1],unsigned char digest2[EVP_MAX_MD_SIZE+1],enum cfhashes type)

{ int i,size = EVP_MAX_MD_SIZE;
 
size = FileHashSize(type);

for (i = 0; i < size; i++)
   {
   if (digest1[i] != digest2[i])
      {
      return false;
      }
   }

return true;
}

/*********************************************************************/

char *HashPrint(enum cfhashes type,unsigned char digest[EVP_MAX_MD_SIZE+1])

{ unsigned int i;
  static char buffer[EVP_MAX_MD_SIZE*4];

switch (type)
   {
   case cf_md5:
       sprintf(buffer,"MD5=  ");
       break;
   default:
       sprintf(buffer,"SHA=  ");
       break;
   }

for (i = 0; i < CF_DIGEST_SIZES[type]; i++)
   {
   sprintf((char *)(buffer+4+2*i),"%02x", digest[i]);
   }

return buffer; 
}    

/***************************************************************/

void PurgeHashes(struct Attributes attr,struct Promise *pp)

/* Go through the database and purge records about non-existent files */

{ CF_DB *dbp;
  CF_DBC *dbcp;
  struct stat statbuf;
  int ret,ksize,vsize;
  char *key;
  void *value;

if (!OpenDB(HASHDB,&dbp))
   {
   return;
   }

/* Acquire a cursor for the database. */

if (!NewDBCursor(dbp,&dbcp))
   {
   CfOut(cf_inform,""," !! Unable to scan hash database");
   return;
   }

 /* Walk through the database and print out the key/data pairs. */

while(NextDB(dbp,dbcp,&key,&ksize,&value,&vsize))
   {
   char *obj = (char *)key + CF_INDEX_OFFSET;

   if (cfstat(obj,&statbuf) == -1)
      {
      if (attr.change.update)
         {         
         if (DeleteComplexKeyDB(dbp,key,ksize))
            {
            cfPS(cf_error,CF_CHG,"",pp,attr,"ALERT: %s file no longer exists!",obj);
            }
         }
      else
         {
         cfPS(cf_error,CF_WARN,"",pp,attr,"ALERT: %s file no longer exists!",obj);
         }
      }

   memset(&key,0,sizeof(key));
   memset(&value,0,sizeof(value));
   }

DeleteDBCursor(dbp,dbcp);
CloseDB(dbp);
}

/*****************************************************************************/

int ReadHash(CF_DB *dbp,enum cfhashes type,char *name,unsigned char digest[EVP_MAX_MD_SIZE+1])

{ char *key;
  int size;
  struct Checksum_Value chk_val;

key = NewIndexKey(type,name,&size);

if (ReadComplexKeyDB(dbp,key,size,(void *)&chk_val,sizeof(struct Checksum_Value)))
   {
   memset(digest,0,EVP_MAX_MD_SIZE+1);
   memcpy(digest,chk_val.mess_digest,EVP_MAX_MD_SIZE+1);
   DeleteIndexKey(key);
   return true;
   }
else
   {
   DeleteIndexKey(key);
   return false;
   }
}

/*****************************************************************************/

int WriteHash(CF_DB *dbp,enum cfhashes type,char *name,unsigned char digest[EVP_MAX_MD_SIZE+1])

{ char *key;
  struct Checksum_Value *value;
  int ret, keysize;

key = NewIndexKey(type,name,&keysize);
value = NewHashValue(digest);
ret = WriteComplexKeyDB(dbp,key,keysize,value,sizeof(struct Checksum_Value));
DeleteIndexKey(key);
DeleteHashValue(value);
return ret;
}

/*****************************************************************************/

void DeleteHash(CF_DB *dbp,enum cfhashes type,char *name)

{ int size;
  char *key;

key = NewIndexKey(type,name,&size);  
DeleteComplexKeyDB(dbp,key,size);
DeleteIndexKey(key);
}

/*****************************************************************************/
/* level                                                                     */
/*****************************************************************************/

char *NewIndexKey(char type,char *name, int *size)

{ char *chk_key;

// Filename plus index_str in one block + \0

*size = strlen(name)+CF_INDEX_OFFSET+1;
 
if ((chk_key = malloc(*size)) == NULL)
   {
   FatalError("NewIndexKey malloc error");
   }

// Data start after offset for index

memset(chk_key,0,*size);

strncpy(chk_key,FileHashName(type),CF_INDEX_FIELD_LEN);
strncpy(chk_key+CF_INDEX_OFFSET,name,strlen(name));
return chk_key;
}

/*****************************************************************************/

void DeleteIndexKey(char *key)

{
free(key);
}

/*****************************************************************************/

struct Checksum_Value *NewHashValue(unsigned char digest[EVP_MAX_MD_SIZE+1])
    
{ struct Checksum_Value *chk_val;

if ((chk_val = (struct Checksum_Value *)malloc(sizeof(struct Checksum_Value))) == NULL)
   {
   FatalError("NewHashValue malloc error");
   }

memset(chk_val,0,sizeof(struct Checksum_Value));
memcpy(chk_val->mess_digest,digest,EVP_MAX_MD_SIZE+1);

/* memcpy(chk_val->attr_digest,attr,EVP_MAX_MD_SIZE+1); depricated */

return chk_val;
}

/*****************************************************************************/

void DeleteHashValue(struct Checksum_Value *chk_val)

{
free((char *)chk_val);
}

/*********************************************************************/

char *FileHashName(enum cfhashes id)

{
return CF_DIGEST_TYPES[id][0];
}

/***************************************************************************/

int FileHashSize(enum cfhashes id)

{ int i,size = 0;
 
return CF_DIGEST_SIZES[id];
}
