package gov.fnal.frontier;

import java.sql.*;
import java.io.*;
import gov.fnal.frontier.codec.*;
import java.util.*;

public class Payload
 {
  private static Hashtable htFdo=new Hashtable(); 
  
  private DbConnectionMgr dbm;
  private Command cmd;
  private FrontierDataObject fdo;
  
  private static final String fds_sql=
   "select xsd_type,xsd_data from "+Frontier.getXsdTableName()+" where name = ? and version = ? ";
   
  public boolean noCache=false;
  public long time_expire;
  public String type;
  public String version;
  public String encoder;
  public int err_code;
  public String err_msg;
  public String md5;
  public int rec_num;
  
  protected Payload(Command a_cmd,DbConnectionMgr a_dbm) throws Exception
   {
    dbm=a_dbm;
    cmd=a_cmd;        
    System.out.println("New payload for cmd="+cmd);
    type=cmd.obj_name;
    version=cmd.obj_version;
    encoder=cmd.encoder;
    err_code=0;
    err_msg="";
    md5="";
    rec_num=0;
        
    String key="_fdo__"+cmd.obj_name+"_@<:>@__"+cmd.obj_version;
    
    // XXX - during development ONLY!
    //fdo=(FrontierDataObject)htFdo.get(key);
    if(fdo!=null)
     {
      System.out.println("Got "+key+" from cache");
      time_expire=fdo.fdo_get_expiration_time();
      noCache=fdo.fdo_is_no_cache();
      return;      
     }
    
    Connection con=null;
    PreparedStatement stmt=null;
    ResultSet rs=null;
    try
     {
      con=dbm.getDescriptorConnection();
      stmt=con.prepareStatement(fds_sql);
      stmt.setString(1,cmd.obj_name);
      stmt.setString(2,cmd.obj_version);
      rs=stmt.executeQuery();
      if(!rs.next()) throw new Exception("Object "+cmd.obj_name+":"+cmd.obj_version+" does not exists");
      String xsd_type=rs.getString("xsd_type");      
      Blob blob=rs.getBlob("xsd_data");
      int len=(int)blob.length();
      byte[] b=blob.getBytes((long)1,len);
      if(xsd_type.equals("xml"))
       {
        fdo=new XsdDataObject(dbm);
        fdo.fdo_init(b);
        time_expire=fdo.fdo_get_expiration_time();
        noCache=fdo.fdo_is_no_cache();
        htFdo.put(key,fdo);
       }
      else throw new Exception("Unsupported xsd_type "+xsd_type+".");
     }
    finally
     {
      if(rs!=null) try{rs.close();}catch(Exception e){}
      if(stmt!=null) try{stmt.close();}catch(Exception e){}
      if(con!=null) try{dbm.release(con);}catch(Exception e){}
     }     
   }
   
   
  public void send(OutputStream out) throws Exception
   {
    Encoder enc=null;
    rec_num=0;
    
    switch(cmd.cmd_domain)
     {
      case Command.CMD_GET:
       enc=new BlobTypedEncoder(out);
       try { rec_num=fdo.fdo_get(enc,cmd.method,cmd.fds); }
       finally 
        { 
         enc.close(); 
         md5=md5Digest(enc);
        }
       return;
      
      case Command.CMD_META:
       enc=new BlobTypedEncoder(out);
       try { rec_num=fdo.fdo_meta(enc); }
       finally 
        { 
         enc.close(); 
         md5=md5Digest(enc);
        }
       return;
       
      default:
       throw new Exception("Unsupported command domain "+cmd.cmd_domain);
     }
   }
   
   
  private String md5Digest(Encoder encoder) throws Exception
   {
    StringBuffer md5_ascii=new StringBuffer("");
    byte[] md5_digest=encoder.getMD5Digest();
    for(int i=0;i<md5_digest.length;i++) 
     {
      int v=(int)md5_digest[i];
      if(v<0)v=256+v;
      String str=Integer.toString(v,16);
      if(str.length()==1)md5_ascii.append("0");
      md5_ascii.append(str);
     }
    return md5_ascii.toString();
   }  
 }
