package gov.fnal.frontier.util.xsdloader;

import java.io.*;
import org.xml.sax.*;
import org.xml.sax.helpers.*;
import java.util.*;



public class Xsd extends DefaultHandler
 {
  private static final String VALIDATION_FEATURE_ID="http://xml.org/sax/features/validation";
  private static final String DEFAULT_PARSER_NAME="org.apache.xerces.parsers.SAXParser";
  
  // Public stuff
  public String obj_name;
  public String obj_version;
  public String xsd_version;
  
   
  protected Xsd()
   {
   }

   
  private String prepareXml(byte[] body) throws Exception
   {
    // StringBuffer can not append byte[] - isn't it silly?
    String str=new String(body,"US-ASCII");
    return str;
   }
   
      
  public void init(byte[] body) throws Exception
   {
    String xml=prepareXml(body);
    XMLReader parser=null;
    boolean validation=false;

    parser=XMLReaderFactory.createXMLReader(DEFAULT_PARSER_NAME);
    try 
     {
      parser.setFeature(VALIDATION_FEATURE_ID, validation);
     }
    catch(SAXException e) 
     {
      throw new Exception("Error: Parser does not support feature ("+VALIDATION_FEATURE_ID+"): "+e);
     }
    
    parser.setContentHandler(this);
    parser.setErrorHandler(this);
    StringReader sr=new StringReader(xml);
    InputSource is=new InputSource(sr);
    parser.parse(is);
   }
   
      
  /********************************************
   ********************************************
   * SAX related stuff is below   
   *
   ********************************************/   
   
   
  private void print_attr(Attributes attrs) throws SAXException
   {
    for(int i=0;attrs!=null && i<attrs.getLength();i++) 
     {
      //System.out.println("attr "+attrs.getQName(i)+":"+attrs.getValue(i)+".");
     }
   }
   
   
  public void startDocument() throws SAXException 
   {
    //System.out.println("Start document");
   }
   
  
  public void startElement(String uri,String local,String raw, Attributes attrs) throws SAXException 
   {
    //System.out.println("startElem u="+uri+",l="+local+",r="+raw+".");

    // print_attrs(attrs);
        
    if(local.equals("descriptor"))
     {
      obj_name=attrs.getValue("type");
      obj_version=attrs.getValue("version");
      xsd_version=attrs.getValue("xsdversion");
      System.out.println("Detected "+obj_name+":"+obj_version+", "+xsd_version+".");
      return;
     }
   }   
   
  /** Warning. */
  public void warning(SAXParseException ex) throws SAXException 
   {
    System.out.println("Warning: "+ex);
    ex.printStackTrace();
   }

  /** Error. */
  public void error(SAXParseException ex) throws SAXException 
   {
    System.out.println("Error: "+ex);
    throw ex;
   }

  /** Fatal error. */
  public void fatalError(SAXParseException ex) throws SAXException 
   {
    System.out.println("Fatal: "+ex);
    throw ex;
   }   
 }
