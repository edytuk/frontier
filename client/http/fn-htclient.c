/*
 * frontier client http implementation
 * 
 * Author: Sergey Kosyakov
 *
 * $Id$
 *
 *  Copyright (C) 2007  Fermilab
 *
 *  This program is free software: you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
 
#include <fn-htclient.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_NAME_LEN	256
#define URL_FMT_SRV	"http://%127[^/]/%127s"
#define URL_FMT_PROXY	"http://%s"

extern void *(*frontier_mem_alloc)(size_t size);
extern void (*frontier_mem_free)(void *p);

#define PERSISTCONNECTION 

FrontierHttpClnt *frontierHttpClnt_create(int *ec)
 {
  FrontierHttpClnt *c;
  
  c=frontier_mem_alloc(sizeof(FrontierHttpClnt));
  if(!c)
   {
    *ec=FRONTIER_EMEM;
    FRONTIER_MSG(*ec);
    return c;
   }
  bzero(c,sizeof(FrontierHttpClnt));
  c->socket=-1;
  c->frontier_id=(char*)0;
  c->data_pos=0;
  c->data_size=0;
  c->content_length=-1;
  c->url_suffix="";
  c->rand_seed=getpid();
  
  *ec=FRONTIER_OK;
  return c;
 }
 
void frontierHttpClnt_setConnectTimeoutSecs(FrontierHttpClnt *c,int timeoutsecs)
 {
  c->connect_timeout_secs=timeoutsecs;
 }
 
void frontierHttpClnt_setReadTimeoutSecs(FrontierHttpClnt *c,int timeoutsecs)
 {
  c->read_timeout_secs=timeoutsecs;
 }
 
void frontierHttpClnt_setWriteTimeoutSecs(FrontierHttpClnt *c,int timeoutsecs)
 {
  c->write_timeout_secs=timeoutsecs;
 }
 
int frontierHttpClnt_addServer(FrontierHttpClnt *c,const char *url)
 {
  FrontierUrlInfo *fui;
  int ec=FRONTIER_OK;
  
  fui=frontier_CreateUrlInfo(url,&ec);
  if(!fui) return ec;
  
  if(!fui->path)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"config error: server %s: servlet path is missing",fui->host);
    frontier_DeleteUrlInfo(fui);
    return FRONTIER_ECFG;
   }
   
  if(c->total_server>=(sizeof(c->server)/sizeof(c->server[0]))) 
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"config error: server list is too long");
    frontier_DeleteUrlInfo(fui);    
    return FRONTIER_ECFG;
   }
  
  c->server[c->total_server]=fui;
  c->total_server++;
  if(c->balance_servers)frontierHttpClnt_setBalancedServers(c);
  
  return FRONTIER_OK;
 }

 
int frontierHttpClnt_addProxy(FrontierHttpClnt *c,const char *url)
 {
  FrontierUrlInfo *fui;
  int ec=FRONTIER_OK;
  
  fui=frontier_CreateUrlInfo(url,&ec);
  if(!fui) return ec;
  
  if(c->total_proxy>=(sizeof(c->proxy)/sizeof(c->proxy[0])))
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"config error: proxy list is too long");
    frontier_DeleteUrlInfo(fui);    
    return FRONTIER_ECFG;
   }
    
  c->proxy[c->total_proxy]=fui;
  c->total_proxy++;
  if(c->balance_proxies)frontierHttpClnt_setBalancedProxies(c);

  return FRONTIER_OK;
 }
 
 
 
void frontierHttpClnt_setCacheRefreshFlag(FrontierHttpClnt *c,int is_refresh)
 {
  c->is_refresh=is_refresh;
 }

 
void frontierHttpClnt_setUrlSuffix(FrontierHttpClnt *c,char *suffix)
 {
  /* note that no copy is made -- caller must insure longevity of suffix */
  c->url_suffix=suffix;
 }

 
void frontierHttpClnt_setFrontierId(FrontierHttpClnt *c,const char *frontier_id)
 {
  int i;
  int len;
  char *p;
  unsigned char ch;
  
  len=strlen(frontier_id);
  if(len>MAX_NAME_LEN) len=MAX_NAME_LEN;
  
  if(c->frontier_id) frontier_mem_free(c->frontier_id);
  c->frontier_id=frontier_mem_alloc(len+1);
  p=c->frontier_id;
  
  // eliminate any illegal characters
  for(i=0;i<len;i++)  
   {
    ch=frontier_id[i];
    if(isalnum(ch)||strchr(" ;:,.<>/?=+-_{}[]()|@",ch))
      *p++=ch;
   }
  *p=0;
 } 
 
 
 
static int http_read(FrontierHttpClnt *c)
 {
  //printf("\nhttp_read\n");
  //bzero(c->buf,FRONTIER_HTTP_BUF_SIZE);
  c->data_pos=0;  
  c->data_size=frontier_read(c->socket,c->buf,FRONTIER_HTTP_BUF_SIZE,c->read_timeout_secs,c->cur_addr);

  return c->data_size;
 }

 
static int read_char(FrontierHttpClnt *c,char *ch)
 {
  int ret;
  
  if(c->data_pos+1>c->data_size)
   {
    ret=http_read(c);
    if(ret<=0) return ret;
   }
  *ch=c->buf[c->data_pos];
  c->data_pos++;
  return 1;
 }

 
static int test_char(FrontierHttpClnt *c,char *ch)
 {
  int ret;
  
  if(c->data_pos+1>c->data_size)
   {
    ret=http_read(c);
    if(ret<=0) return ret;
   }
  *ch=c->buf[c->data_pos];
  return 1;
 }
 
 
static int read_line(FrontierHttpClnt *c,char *buf,int buf_len)
 {
  int i;
  int ret;
  char ch,next_ch;
  
  i=0;
  while(1)
   {
    ret=read_char(c,&ch);
    if(ret<0) return ret;
    if(!ret) break;
    
    if(ch=='\r' || ch=='\n')
     {
      ret=test_char(c,&next_ch);
      if(ret<0) return ret;
      if(!ret) break;      
      if(ch=='\r' && next_ch=='\n') read_char(c,&ch);	// Just ignore
      break;
     }
    buf[i]=ch;
    i++;
    if(i>=buf_len-1) break;
   }
  buf[i]=0;
  return i;
 }
   
  
static int open_connection(FrontierHttpClnt *c)
 {
  int ret;
  struct sockaddr_in *sin;
  struct addrinfo *addr;
  FrontierUrlInfo *fui_proxy,*fui_server,*fui;
  
  if(c->socket!=-1)
   {
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"reusing persisted connection s=%d",c->socket);
    return 0;
   }

  frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"no persisted connection found, opening new");

  fui_proxy=c->proxy[c->cur_proxy];
  fui_server=c->server[c->cur_server];
  
  if(!fui_server)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"no available server");
    return FRONTIER_ECFG;
   }   
  
  if(fui_proxy)
   {
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"connecting to proxy %s",fui_proxy->host);
    fui=fui_proxy;
    c->using_proxy=1;
   }
  else
   {
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"connecting to server %s",fui_server->host);
    fui=fui_server;
    c->using_proxy=0;
   }
     
  ret=frontier_resolv_host(fui);
  if(ret) return ret;
  addr=fui->fai->addr;

  c->socket=frontier_socket();
  if(c->socket<0) return c->socket;
  
  sin=(struct sockaddr_in*)(addr->ai_addr);
  sin->sin_port=htons((unsigned short)(fui->port));
     
  ret=frontier_connect(c->socket,addr->ai_addr,addr->ai_addrlen,c->connect_timeout_secs);

  if(ret!=FRONTIER_OK)
   {
    frontier_socket_close(c->socket);
    c->socket=-1;
   }

  c->cur_addr=addr;
   
  return ret;
}


#define FN_REQ_BUF 8192

static int get_url(FrontierHttpClnt *c,const char *url,int is_post)
{
  int ret;
  int len;  
  char buf[FN_REQ_BUF];
  FrontierUrlInfo *fui_server;
  char *http_method;
  
  http_method=is_post?"POST":"GET";

  fui_server=c->server[c->cur_server];

  bzero(buf,FN_REQ_BUF);
  
  if(c->using_proxy)
   {
    len=snprintf(buf,FN_REQ_BUF,"%s %s/%s%s HTTP/1.0\r\nHost: %s\r\n",http_method,fui_server->url,url,c->url_suffix,fui_server->host);
   }
  else
   {
    len=snprintf(buf,FN_REQ_BUF,"%s /%s/%s%s HTTP/1.0\r\nHost: %s\r\n",http_method,fui_server->path,url,c->url_suffix,fui_server->host);
   }
  if(len>=FN_REQ_BUF)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"request is bigger than %d bytes",FN_REQ_BUF);
    return FRONTIER_EIARG;
   }
  
  // Would use 'Cache-Control: max-stale=0' but squid 2.6 series (at least
  //  2.6STABLE13 and 2.6STABLE18, but not 2.7STABLE4) then sends just 
  //  'Cache-Control: max-stale' (without =0) upstream and messes up
  //  its parent.  max-stale=1 is almost the same thing except for one
  //  second, and requires squid to return an error if server is down
  //  rather than returning stale data.  This option has very little
  //  effect, but it gets sent upstream to frontier server which now
  //  can send back a corresponding stale-if-error header which does
  //  tell squid 2.7 or later to return a 504 error if the origin server
  //  can't be reached when a cached item is stale.  frontier_client
  //  couldn't handle that before this header was added, so this allows
  //  a compatible upgrade
#ifdef PERSISTCONNECTION
  ret=snprintf(buf+len,FN_REQ_BUF-len,"X-Frontier-Id: %s\r\nCache-Control: max-stale=1\r\nConnection: keep-alive\r\n",c->frontier_id);
#else
  ret=snprintf(buf+len,FN_REQ_BUF-len,"X-Frontier-Id: %s\r\nCache-Control: max-stale=1\r\n",c->frontier_id);
#endif
  if(ret>=FN_REQ_BUF-len)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"request is bigger than %d bytes",FN_REQ_BUF);
    return FRONTIER_EIARG;
   }
  len+=ret;
   
  // POST is always no-cache
  if(is_post || c->is_refresh)
   {
    ret=snprintf(buf+len,FN_REQ_BUF-len,"Pragma: no-cache\r\n\r\n");
   }
  else
   {
    ret=snprintf(buf+len,FN_REQ_BUF-len,"\r\n");
   }
  if(ret>=FN_REQ_BUF-len)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"request is bigger than %d bytes",FN_REQ_BUF);
    return FRONTIER_EIARG;
   }
  len+=ret;
   
  frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"request <%s>",buf);
   
  ret=frontier_write(c->socket,buf,len,c->write_timeout_secs,c->cur_addr);
  if(ret<0) return ret;
  return FRONTIER_OK;
 }
 
 
static int read_connection(FrontierHttpClnt *c)
 {
  int ret;
  int tot=0;
  char buf[FN_REQ_BUF];
  
  // Read status line
  ret=read_line(c,buf,FN_REQ_BUF);
  if(ret<=0) return ret;
  tot=ret;
  frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"status line <%s>",buf);
  if((strncmp(buf,"HTTP/1.0 5",10)==0)||(strncmp(buf,"HTTP/1.1 5",10)==0))
   {
    /* 5xx HTTP error code indicates server error */
    frontier_setErrorMsg(__FILE__,__LINE__,"server error (%s)",buf);
    return FRONTIER_ESERVER;
   }

  if((strncmp(buf,"HTTP/1.0 200 ",13)!=0)&&(strncmp(buf,"HTTP/1.1 200 ",13)!=0))
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"bad server response (%s)",buf);
    return FRONTIER_EPROTO;
   }
      
  do
   {   
    ret=read_line(c,buf,FN_REQ_BUF);
    if(ret<0) return ret;
    tot+=ret;
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"got line <%s>",buf);
    /* look for "content-length" header */
#define CLHEAD "content-length: "
#define CLLEN (sizeof(CLHEAD)-1)
    if((c->content_length==-1)&&(ret>CLLEN)&&(strncasecmp(buf,CLHEAD,CLLEN)==0))
      c->content_length=atoi(buf+CLLEN);
#undef CLHEAD
#undef CLLEN
   }while(*buf);

  return tot; 
 }
 

int frontierHttpClnt_open(FrontierHttpClnt *c)
 {
  int ret;
  
  ret=open_connection(c);
  return ret;
 }
 
 
int frontierHttpClnt_get(FrontierHttpClnt *c,const char *url)
 {
  return frontierHttpClnt_post(c,url,0);
 }


int frontierHttpClnt_post(FrontierHttpClnt *c,const char *url,const char *body)
 {
  int ret;
  int len=body?strlen(body):0;
  int try=0;

  for(try=0;try<2;try++)
   {
    ret=get_url(c,url,0);
    if(ret) return ret;
  
    if(len>0)
     {
      frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"body (%d bytes): [\n%s\n]",len,body);
      ret=frontier_write(c->socket,body,len,c->write_timeout_secs,c->cur_addr);
      if(ret<0) return ret;
     }

    ret=read_connection(c);
    if(ret==0)
     {
      if(try==0)
       {
        /*An empty response can happen on persisted connections after*/
	/*long waits.  Close, reopen, and retry.*/
        frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"got empty response, re-connecting and retrying");
        frontierHttpClnt_close(c);
        frontierHttpClnt_open(c);
	continue;
       }
      frontier_setErrorMsg(__FILE__,__LINE__,"empty response from server");
      return FRONTIER_ENETWORK;    
     }
    if((ret==FRONTIER_ESYS)&&(try==0))
     {
      /*System error 104 Connection reset by peer has been seen on*/
      /*heavily-loaded localhost squids (on a node with 8 cores).*/
      /*Close, reopen, and retry.*/
      frontier_log(FRONTIER_LOGLEVEL_WARNING,__FILE__,__LINE__,"Retrying after system error");
      frontierHttpClnt_close(c);
      frontierHttpClnt_open(c);
      continue;
     }
    /*if there was no continue, don't try again*/
    break;
   }

  if(ret>0)return FRONTIER_OK;
  return ret;
 }
 
 
int frontierHttpClnt_read(FrontierHttpClnt *c,char *buf,int buf_len)
 {
  int ret;
  int available;
  
#ifdef PERSISTCONNECTION
  if(c->content_length==0)
    /* no more data to read for this url */
    return 0;
#endif

  if(c->data_pos+1>c->data_size)
   {
    ret=http_read(c);
    if(ret<=0) return ret;
   }
   
  available=c->data_size-c->data_pos;
  available=buf_len>available?available:buf_len;
#ifdef PERSISTCONNECTION
  if(c->content_length>0)
   {
    if(available>c->content_length)
     {
      frontier_setErrorMsg(__FILE__,__LINE__,"More data available (%d bytes) than Content-Length (%d bytes) says should be there",available,c->content_length);
      return -1;
     }
    c->content_length-=available;
   }
  c->total_length+=available;
#endif
  bcopy(c->buf+c->data_pos,buf,available);
  c->data_pos+=available;

#if 0
   {
    /* print the beginning of the data in the block */
    char savech;
    int lineno;
    char *p = buf;
    for (lineno = 0; lineno < 15; lineno++)
     {
      if ((lineno+1) * 40 >= available)
       {
	break;
       }
      savech = p[40];
      p[40] = '\0';
      frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"data [%s]",p);
      p[40] = savech;
      p+=40;
     }
     if (lineno == 15)
        frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"(more data not logged)");
   }
#endif
    
  return available;
 }
 
 
void frontierHttpClnt_close(FrontierHttpClnt *c)
 {
#ifdef PERSISTCONNECTION
  if(c->content_length<0)
   {
    /*there was no Content-Length header or this function was called twice*/
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"closing persistent connection");
    /*note that "Proxy-Connection: close" was probably also set*/
    frontier_socket_close(c->socket);
    c->socket=-1;
   }
  else if(c->total_length>=FRONTIER_MAX_PERSIST_SIZE)
   {
    /*squid inconsistently drops persistent connections after large objects
      (at least in squid2.6STABLE13, it drops after those that were cache
      MISSes upstream when the object initially loaded), so make it consistent
      and close after every large object.  This helps with load balancing.*/
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"closing persistent connection after large object");
    frontier_socket_close(c->socket);
    c->socket=-1;
   }
  else
   {
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"persisting connection s=%d",c->socket);
   }
  c->total_length=0;
#else
  frontier_socket_close(c->socket);
  c->socket=-1;
#endif
  c->data_pos=0;
  c->data_size=0;  
  c->content_length=-1;
 }
 
void frontierHttpClnt_delete(FrontierHttpClnt *c)
 {
  int i;
  
  if(!c) return;
  
  for(i=0;i<(sizeof(c->server)/sizeof(c->server[0])); i++)
    frontier_DeleteUrlInfo(c->server[i]);
  for(i=0;i<(sizeof(c->proxy)/sizeof(c->proxy[0])); i++)
    frontier_DeleteUrlInfo(c->proxy[i]);
  
  if(c->frontier_id) frontier_mem_free(c->frontier_id);
  
  frontier_mem_free(c);
 }

int frontierHttpClnt_resetproxylist(FrontierHttpClnt *c,int shuffle)
 {
  /*don't reset anything if connection is persisting*/
  if(c->socket==-1)
   {
    if(shuffle)
     {
      int i;
      if(c->balance_proxies)
       {
	// randomize start if balancing was selected
	// ignore the possibility that these addresses may include round-robin
	int numgood=0;
	for(i=0;i<c->total_proxy;i++)
	  if(!c->proxy[i]->fai->haderror)
	    numgood++;
	if(numgood>0)
	 {
	  c->first_proxy=rand_r(&c->rand_seed)%numgood;
	  // skip the ones that had an error
	  for(i=0;i<=c->first_proxy;i++)
	    if(c->proxy[i]->fai->haderror)
	      c->first_proxy++;
	 }
       }
      else
       {
	for(i=0;(i<=c->cur_proxy)&&(i<c->total_proxy);i++)
	 {
	  // select next round-robin address if any for each that has been used
	  FrontierUrlInfo *fui=c->proxy[i];
	  if ((fui->fai=fui->fai->next)==0)
	    fui->fai=&fui->firstfai;
	  fui->lastfai=fui->fai;
	 }
       }
     }
    c->cur_proxy=c->first_proxy;
    if(c->cur_proxy>=c->total_proxy)
      return(-1);
    if(c->proxy[c->cur_proxy]->fai->haderror)
     {
      /*find one without an error*/
      frontierHttpClnt_nextproxy(c,0);
     }
   }
  return(c->cur_proxy);
 }

int frontierHttpClnt_resetserverlist(FrontierHttpClnt *c,int shuffle)
 {
  /*don't reset anything if connection is persisting*/
  if(c->socket==-1)
   {
    if(shuffle)
     {
      int i;
      if(c->balance_servers)
       {
	// randomize start if balancing was selected
	// ignore the possibility that these addresses may include round-robin
	int numgood=0;
	for(i=0;i<c->total_server;i++)
	  if(!c->server[i]->fai->haderror)
	    numgood++;
	if(numgood>0)
	 {
	  c->first_server=rand_r(&c->rand_seed)%numgood;
	  // skip the ones that had an error
	  for(i=0;i<=c->first_server;i++)
	    if(c->server[i]->fai->haderror)
	      c->first_server++;
	 }
       }
      else
       {
	for(i=0;(i<=c->cur_server)&&(i<c->total_server);i++)
	 {
	  // select next round-robin address if any for each that has been used
	  FrontierUrlInfo *fui=c->server[i];
	  if ((fui->fai=fui->fai->next)==0)
	    fui->fai=&fui->firstfai;
	  fui->lastfai=fui->fai;
	 }
       }
     }
    c->cur_server=c->first_server;
    if(c->cur_server>=c->total_server)
      return(-1);
    if(c->server[c->cur_server]->fai->haderror)
     {
      /*find one without an error*/
      frontierHttpClnt_nextserver(c,0);
     }
   }
  return(c->cur_server);
 }

int frontierHttpClnt_nextproxy(FrontierHttpClnt *c,int curhaderror)
 {
  FrontierUrlInfo *fui=c->proxy[c->cur_proxy];
  if(c->socket!=-1)
   {
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"closing persistent connection after proxy error\n");
    frontier_socket_close(c->socket);
    c->socket=-1;
   }
  if(curhaderror)
    fui->fai->haderror=1;
  else
   {
    // didn't have an error just now, instead looking for next available proxy;
    // advance the round-robin if there is any
    if ((fui->fai=fui->fai->next)==0)
      fui->fai=&fui->firstfai;
   }
  if(curhaderror||(fui->lastfai==fui->fai))
   {
    /*cycle through proxy list*/
    c->cur_proxy++;
    if(c->cur_proxy==c->total_proxy)
      /*wrap around in case doing load balancing*/
      c->cur_proxy=0;
    if(c->cur_proxy==c->first_proxy)
      /*set to total when done*/
      c->cur_proxy=c->total_proxy;
   }
  if(c->cur_proxy>=c->total_proxy)
   {
    /* Exhausted list.  For each proxy in which all addresses have had
       errors, clear the errors for next try.  */
    int i;
    for(i=0;i<c->total_proxy;i++)
     {
      FrontierAddrInfo *fai;
      int anygood=0;
      for(fai=&c->proxy[i]->firstfai;fai!=0;fai=fai->next)
       {
	if(!fai->haderror)
	  anygood=1;
	if(c->balance_proxies)
	  break; // other round-robin addresses are ignored when balancing
       }
      if(!anygood)
       {
        for(fai=&c->proxy[i]->firstfai;fai!=0;fai=fai->next)
	  fai->haderror=0;
       }
     }
    return(-1);
   }
  if(c->proxy[c->cur_proxy]->fai->haderror)
    return(frontierHttpClnt_nextproxy(c,curhaderror));
  return(c->cur_proxy);
 }

int frontierHttpClnt_nextserver(FrontierHttpClnt *c,int curhaderror)
 {
  FrontierUrlInfo *fui=c->server[c->cur_server];
  if(c->socket!=-1)
   {
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"closing persistent connection after server error\n");
    frontier_socket_close(c->socket);
    c->socket=-1;
   }
  if(curhaderror)
    fui->fai->haderror=1;
  else
   {
    // didn't have an error just now, instead looking for next available server;
    // advance the round-robin if there is any
    if ((fui->fai=fui->fai->next)==0)
      fui->fai=&fui->firstfai;
   }
  if(curhaderror||(fui->lastfai==fui->fai))
   {
    /*cycle through server list*/
    c->cur_server++;
    if(c->cur_server==c->total_server)
      /*wrap around in case doing load balancing*/
      c->cur_server=0;
    if(c->cur_server==c->first_server)
      /*set to total when done*/
      c->cur_server=c->total_server;
   }
  if(c->cur_server>=c->total_server)
   {
    /*this query is done, but re-set in case another query is made*/
    c->cur_server=c->first_server;
    return(-1);
   }
  if(c->server[c->cur_server]->fai->haderror)
    return(frontierHttpClnt_nextserver(c,curhaderror));
  return(c->cur_server);
 }


char *frontierHttpClnt_curproxyname(FrontierHttpClnt *c)
 {
  if (c->cur_proxy<c->total_proxy)
    return c->proxy[c->cur_proxy]->host;
  return "";
 }

char *frontierHttpClnt_curservername(FrontierHttpClnt *c)
 {
  if (c->cur_server<c->total_server)
    return c->server[c->cur_server]->host;
  return "";
 }

char *frontierHttpClnt_curserverpath(FrontierHttpClnt *c)
 {
  if (c->cur_server<c->total_server)
    return c->server[c->cur_server]->path;
  return "";
 }

void frontierHttpClnt_setBalancedProxies(FrontierHttpClnt *c)
 {
  c->balance_proxies=1;
  frontierHttpClnt_resetproxylist(c,1);
 }

void frontierHttpClnt_setBalancedServers(FrontierHttpClnt *c)
 {
  c->balance_servers=1;
  frontierHttpClnt_resetserverlist(c,1);
 }

