#if defined(USE_SSL)

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>

#include "sip2/sipstack/SipStack.hxx"
#include "sip2/sipstack/Security.hxx"
#include "sip2/sipstack/Contents.hxx"
#include "sip2/sipstack/Pkcs7Contents.hxx"
#include "sip2/sipstack/PlainContents.hxx"
#include "sip2/util/Logger.hxx"
#include "sip2/util/Random.hxx"
#include "sip2/util/DataStream.hxx"


using namespace Vocal2;

#define VOCAL_SUBSYSTEM Subsystem::SIP



Security::Security()
{
   privateKey = NULL;
   publicCert = NULL;

   static bool initDone=false;
   if ( !initDone )
   {
      initDone = true;
      
      OpenSSL_add_all_algorithms();
      ERR_load_crypto_strings();

      Random::initialize();
   }
}


Security::~Security()
{
}


bool 
Security::loadAllCerts( const Data& password, char* directoryPath )
{
   bool ok = true;
   ok = loadMyPublicCert() ? ok : false;
   ok = loadMyPrivateKey(password) ? ok : false;

   return ok;
}
     

bool 
Security::loadMyPublicCert( char* filePath )
{
   if ( !filePath )
   {
      filePath = "/home/cullen/certs/id.pem";
   }
   
   FILE* fp = fopen(filePath,"r");
   if ( !fp )
   {
      ErrLog( << "Could not read public cert from " << filePath );
      return false;
   }
   
   publicCert = PEM_read_X509(fp,NULL,NULL,NULL);
   if (!publicCert)
   {
      ErrLog( << "Error reading contents of public cert file " << filePath );
      return false;
   }
   
   DebugLog( << "Loaded public cert");
   
   return true;
}


bool 
Security::loadMyPrivateKey( const Data& password, char* filePath )
{
   if ( !filePath )
   {
      filePath = "/home/cullen/certs/id_key.pem";
   }
   
   FILE* fp = fopen(filePath,"r");
   if ( !fp )
   {
      ErrLog( << "Could not read private key from " << filePath );
      return false;
   }
   
   //DebugLog( "password is " << password );
   
   privateKey = PEM_read_PrivateKey(fp,NULL,NULL,(void*)password.c_str());
   if (!privateKey)
   {
      ErrLog( << "Error reading contents of private key file " << filePath );

      while (1)
      {
         const char* file;
         int line;
         
         unsigned long code = ERR_get_error_line(&file,&line);
         if ( code == 0 )
         {
            break;
         }

         char buf[256];
         ERR_error_string_n(code,buf,sizeof(buf));
         ErrLog( << buf  );
         DebugLog( << "Error code = " << code << " file=" << file << " line=" << line );
      }
      
      return false;
   }
   
   DebugLog( << "Loaded private key");
   
   return true;
}



Pkcs7Contents* 
Security::sign( Contents* bodyIn )
{
   assert( bodyIn );
   
   Data bodyData;
   oDataStream strm(bodyData);
   bodyIn->encode( strm );
   strm.flush();
   
   DebugLog( << "body data to sign is <" << bodyData << ">" );
      
   const char* p = bodyData.data();
   int s = bodyData.size();
   
   BIO* in;
   in = BIO_new_mem_buf( (void*)p,s);
   assert(in);
   DebugLog( << "ceated in BIO");
    
   BIO* out;
   out = BIO_new(BIO_s_mem());
   assert(out);
   DebugLog( << "created out BIO" );
     
   STACK_OF(X509)* chain=NULL;
   chain = sk_X509_new_null();
   assert(chain);

   DebugLog( << "checking" );
   assert( publicCert );
   assert( privateKey );
   
   int i = X509_check_private_key(publicCert, privateKey);
   DebugLog( << "checked cert and key ret=" << i  );
   
   PKCS7* pkcs7 = PKCS7_sign( publicCert, privateKey, chain, in, 0 /*flags*/);
   if ( !pkcs7 )
   {
       ErrLog( << "Error creating PKCS7 signing object" );
      return NULL;
   }
    DebugLog( << "created PKCS7 sign object " );

#if 0
   if ( SMIME_write_PKCS7(out,pkcs7,in,0) != 1 )
   {
      ErrLog( << "Error doind S/MIME write of signed object" );
      return NULL;
   }
   DebugLog( << "created SMIME write object" );
#else
   i2d_PKCS7_bio(out,pkcs7);
#endif
   BIO_flush(out);
   
   char* outBuf=NULL;
   long size = BIO_get_mem_data(out,&outBuf);
   assert( size > 0 );
   
   Data outData(outBuf,size);
  
   // DebugLog( << "Signed body is <" << outData << ">" );

   Pkcs7Contents* outBody = new Pkcs7Contents( outData );
   assert( outBody );

   return outBody;
}


Contents* 
Security::uncode( Pkcs7Contents* sBody )
{
   // for now, assume that this is only a singed message 
   
   assert( sBody );
   
   const char* p = sBody->text().data();
   int   s = sBody->text().size();
   
   BIO* in;
   in = BIO_new_mem_buf( (void*)p,s);
   assert(in);
   DebugLog( << "ceated in BIO");
    
   BIO* out;
   out = BIO_new(BIO_s_mem());
   assert(out);
   DebugLog( << "created out BIO" );

#if 0
   BIO* pkcs7Bio=NULL;
   PKCS7* pkcs7 = SMIME_read_PKCS7(in,&pkcs7Bio);
   if ( !pkcs7 )
   {
      ErrLog( << "Problems doing SMIME_read_PKCS7" );
      return NULL;
   }
   if ( pkcs7Bio )
   {
      ErrLog( << "Can not deal with mutlipart mime version stuff " );
      return NULL;
   }  
#else
   BIO* pkcs7Bio=NULL;
   PKCS7* pkcs7 = d2i_PKCS7_bio(in, NULL);
   if ( !pkcs7 )
   {
      ErrLog( << "Problems doing decode of PKCS7 object" );
      return NULL;
   }
    BIO_flush(in);
#endif

   X509_STORE* store;
   store = X509_STORE_new();
   assert( store);
   
   if ( X509_STORE_load_locations(store,"/home/cullen/certs/root.pem",NULL) != 1 )
   {  
      ErrLog( << "Problems doing X509_STORE_load_locations" );
      assert(0);
   }
   
   STACK_OF(X509)* certs;
   certs = sk_X509_new_null();
   assert( certs );
   
   //sk_X509_push(certs, publicCert);
   
   int flags = 0;
   //flags |=  PKCS7_NOVERIFY;
   
   if ( PKCS7_verify(pkcs7, certs, store, pkcs7Bio, out, flags ) != 1 )
   {
      ErrLog( << "Problems doing PKCS7_verify" );
      while (1)
      {
         const char* file;
         int line;
         
         unsigned long code = ERR_get_error_line(&file,&line);
         if ( code == 0 )
         {
            break;
         }

         char buf[256];
         ERR_error_string_n(code,buf,sizeof(buf));
         ErrLog( << buf  );
         DebugLog( << "Error code = " << code << " file=" << file << " line=" << line );
      }
      
      return NULL;
   }

   BIO_flush(out);
   char* outBuf=NULL;
   long size = BIO_get_mem_data(out,&outBuf);
   assert( size > 0 );
   
   Data outData(outBuf,size);
  
   DebugLog( << "uncodec body is <" << outData << ">" );

   PlainContents* outBody = new PlainContents( outData );
   assert( outBody );

   return outBody;  
}

     

#endif

/* ====================================================================
 * The Vovida Software License, Version 1.0  *  * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved. *  * Redistribution and use in source and binary forms, with or without * modification, are permitted provided that the following conditions * are met: *  * 1. Redistributions of source code must retain the above copyright *    notice, this list of conditions and the following disclaimer. *  * 2. Redistributions in binary form must reproduce the above copyright *    notice, this list of conditions and the following disclaimer in *    the documentation and/or other materials provided with the *    distribution. *  * 3. The names "VOCAL", "Vovida Open Communication Application Library", *    and "Vovida Open Communication Application Library (VOCAL)" must *    not be used to endorse or promote products derived from this *    software without prior written permission. For written *    permission, please contact vocal@vovida.org. * * 4. Products derived from this software may not be called "VOCAL", nor *    may "VOCAL" appear in their name, without prior written *    permission of Vovida Networks, Inc. *  * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL, * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH * DAMAGE. *  * ==================================================================== *  * This software consists of voluntary contributions made by Vovida * Networks, Inc. and many individuals on behalf of Vovida Networks, * Inc.  For more information on Vovida Networks, Inc., please see * <http://www.vovida.org/>. *
 */
