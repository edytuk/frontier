/*
 * FroNTier client API
 * 
 * Author: Sergey Kosyakov
 *
 * $Header$
 *
 * $Id$
 *
 */

#include <frontier-cpp.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

extern "C"
 {
#include <frontier.h>
#include <stdlib.h>
 };

#define RUNTIME_ERROR(m,e) do{throw std::runtime_error(std::string(m)+std::string(": ")+frontier_error_desc(e));}while(0)
#define LOGIC_ERROR(m,e) do{throw std::logic_error(std::string(m)+std::string(": ")+frontier_error_desc(e));}while(0)

using namespace frontier;

Request::~Request()
 {
  if(v_val) {delete v_val; v_val=NULL;}
  if(v_key) {delete v_key; v_key=NULL;}
 }


void frontier::init()
 {
  int ret;

  ret=frontier_init(malloc,free);
  if(ret) RUNTIME_ERROR("Frontier initialization failed",ret);
 }


DataSource::DataSource(const std::string& host_name,int port_number,const std::string& application_path,const std::string& proxy_url)
 {
  int ec=FRONTIER_OK;

  host=host_name;
  port=port_number;
  app_path=application_path;
  proxy=proxy_url;
  url=NULL;
  internal_data=NULL;

  channel=frontier_createChannel(&ec);
  if(ec!=FRONTIER_OK) RUNTIME_ERROR("Can not create frontier channel",ec);
  
  if(proxy.size()>0)
   {
    frontier_setProxy(channel,proxy.c_str(),&ec);
    if(ec!=FRONTIER_OK) RUNTIME_ERROR("Can not register proxy",ec);
   }
 }


void DataSource::getData(const std::vector<const Request*>& v_req)
 {
  int ec;

  if(url) {delete url; url=NULL;}

  if(internal_data) {frontierRSBlob_close((FrontierRSBlob*)internal_data,&ec);internal_data=NULL;}

  std::ostringstream oss;
  char delim='?';
 
  oss << "http://" << host << ":" << port << app_path << "Frontier";
  for(std::vector<const Request*>::size_type i=0;i<v_req.size();i++)
   {
    oss << delim << "type=" << v_req[i]->obj_name << ':' << v_req[i]->v; delim='&';
    const char *enc;
    switch(v_req[i]->enc)
     {
      case BLOB: enc="BLOB"; break;
      default: throw std::logic_error("Unknown encoding requested");
     }
    oss << delim << "encoding=" << enc;
    oss << delim << v_req[i]->key1 << '=' << v_req[i]->val1;

    if(v_req[i]->v_key)
     {
      for(std::vector<std::string>::size_type n=0;n<v_req[i]->v_key->size();n++)
       {
        oss << delim << v_req[i]->v_key->operator[](n) << '=' 
                     << v_req[i]->v_val->operator[](n);
       }
     }
   }

  url=new std::string(oss.str());
  std::cout << "URL <" << *url << ">\n";

  ec=frontier_getRawData(channel,url->c_str());
  if(ec!=FRONTIER_OK) RUNTIME_ERROR("Can not get data",ec);
 }


void DataSource::setCurrentLoad(int n)
 {
  int ec=FRONTIER_OK;
  FrontierRSBlob *rsb=frontierRSBlob_get(channel,n,&ec);
  if(ec!=FRONTIER_OK) LOGIC_ERROR("Can not set current load",ec);
  internal_data=rsb;
 }


unsigned int DataSource::getRecNum()
 {
  if(!internal_data) throw std::logic_error("Current load is not set");
  FrontierRSBlob *rs=(FrontierRSBlob*)internal_data;
  return rs->nrec;
 }


int DataSource::getInt()
 {
  if(!internal_data) throw std::logic_error("Current load is not set");
  FrontierRSBlob *rs=(FrontierRSBlob*)internal_data;
  int ec=FRONTIER_OK;
  int ret=frontierRSBlob_getInt(rs,&ec);
  if(ec!=FRONTIER_OK) LOGIC_ERROR("getInt() failed",ec);
  return ret;
 }


long long DataSource::getLong()
 {
  if(!internal_data) throw std::logic_error("Current load is not set");
  FrontierRSBlob *rs=(FrontierRSBlob*)internal_data;
  int ec=FRONTIER_OK;
  long long ret=frontierRSBlob_getLong(rs,&ec);
  if(ec!=FRONTIER_OK) LOGIC_ERROR("getLong() failed",ec);
  return ret;
 }


double DataSource::getDouble()
 {
  if(!internal_data) throw std::logic_error("Current load is not set");
  FrontierRSBlob *rs=(FrontierRSBlob*)internal_data;
  int ec=FRONTIER_OK;
  double ret=frontierRSBlob_getDouble(rs,&ec);
  if(ec!=FRONTIER_OK) LOGIC_ERROR("getDouble() failed",ec);
  return ret;
 }

 
std::string* DataSource::getString()
 {
  if(!internal_data) throw std::logic_error("Current load is not set");
  FrontierRSBlob *rs=(FrontierRSBlob*)internal_data;
  int ec=FRONTIER_OK;
  int len=frontierRSBlob_getInt(rs,&ec);
  if(ec!=FRONTIER_OK) LOGIC_ERROR("getInt() failed",ec);
  char *s=new char[len];
  for(int i=0;i<len;i++)
   {
    s[i]=frontierRSBlob_getByte(rs,&ec);
    if(ec!=FRONTIER_OK) {delete[] s; LOGIC_ERROR("getInt() failed",ec);}
   }
  std::string *ret=new std::string(s,len);
  delete[] s;
  return ret;
 }
 
 
std::string* DataSource::getBlob()
 {
  return getString();
 }

 

DataSource::~DataSource()
 {
  int ec;
  if(internal_data) {frontierRSBlob_close((FrontierRSBlob*)internal_data,&ec);internal_data=NULL;}
  frontier_closeChannel(channel);
  if(url) delete url;
 }


std::vector<unsigned char>* CDFDataSource::getRawAsArrayUChar()
 {
  std::string *blob=getBlob();
  int len=blob->size();
  std::vector<unsigned char> *ret=new std::vector<unsigned char>(len);
  const char *s=blob->c_str();
  for(int i=0;i<len;i++)
   {
    ret->operator[](i)=s[i];
   }
  delete blob;
  return ret;
 }


std::vector<int>* CDFDataSource::getRawAsArrayInt()
 {
  std::string *blob=getBlob();
  if(blob->size()%4) {delete blob; throw std::logic_error("Blob size is not multiple of 4 - can not convert to int[]");}
  int len=blob->size()/4;
  std::vector<int> *ret=new std::vector<int>(len);
  const char *s=blob->c_str();
  for(int i=0;i<len;i++)
   {
    ret->operator[](i)=frontier_n2h_i32(s+i*4);
   }
  delete blob;
  return ret;
 }
 
   
std::vector<float>* CDFDataSource::getRawAsArrayFloat()
 {
  std::string *blob=getBlob();
  if(blob->size()%4) {delete blob; throw std::logic_error("Blob size is not multiple of 4 - can not convert to float[]");}
  int len=blob->size()/4;
  std::vector<float> *ret=new std::vector<float>(len);
  const char *s=blob->c_str();
  for(int i=0;i<len;i++)
   {
    ret->operator[](i)=frontier_n2h_f32(s+i*4);
   }
  delete blob;
  return ret;
 }


std::vector<double>* CDFDataSource::getRawAsArrayDouble()
 {
  std::string *blob=getBlob();
  if(blob->size()%8) {delete blob; throw std::logic_error("Blob size is not multiple of 8 - can not convert to double[]");}
  int len=blob->size()/8;
  std::vector<double> *ret=new std::vector<double>(len);
  const char *s=blob->c_str();
  for(int i=0;i<len;i++)
   {
    ret->operator[](i)=frontier_n2h_d64(s+i*8);
   }
  delete blob;
  return ret; 
 }


std::vector<long long>* CDFDataSource::getRawAsArrayLong()
 {
  std::string *blob=getBlob();
  if(blob->size()%8) {delete blob; throw std::logic_error("Blob size is not multiple of 8 - can not convert to int64[]");}
  int len=blob->size()/8;
  std::vector<long long> *ret=new std::vector<long long>(len);
  const char *s=blob->c_str();
  for(int i=0;i<len;i++)
   {
    ret->operator[](i)=frontier_n2h_i64(s+i*8);
   }
  delete blob;
  return ret; 
 }
