/*
 *  JsonRpc-Cpp - JSON-RPC implementation.
 *  Copyright (C) 2008-2011 Sebastien Vincent <sebastien.vincent@cppextrem.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file jsonrpc_httpclient.cpp
 * \brief JSON-RPC HTTP client.
 * \author Brian Panneton
 */

#include <sstream>

#include "jsonrpc/restful_httpclient.h"
#include "jsonrpc/netstring.h"

namespace Json
{
  namespace Rpc
  {
	  RestfulHttpClient::RestfulHttpClient()
    {
      this->Initialize();
    }

	RestfulHttpClient::RestfulHttpClient(const std::string& address)
    { 
      this->Initialize();
      this->ChangeAddress(address); 
    }

	RestfulHttpClient::RestfulHttpClient(const std::string& address, uint16_t port, const std::string& username, const std::string& password)
    { 
      this->Initialize();
      this->ChangeAddress(address);
      this->ChangePort(port);
	  this->ChangeUserpwd(username, password);
    }

    void RestfulHttpClient::Initialize()
    {
      /* Init all necessary curl stuff (in windows it will do winsock) */
      curl_global_init(CURL_GLOBAL_ALL);
      m_curlHandle = curl_easy_init();

      /* Set up curl to catch the servers response */
      curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, RestfulHttpClient::StaticRecv);
      curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, this);//Set the userdata argument

	  /*设置超时时间*/
	  curl_easy_setopt(m_curlHandle, CURLOPT_TIMEOUT, 30);

      /* Set up JSON specific HTTP Headers */
      size_t error = 0;
      m_headers = NULL;
     // m_headers = curl_slist_append(m_headers, "Content-Type: application/json-rpc");
	  m_headers = curl_slist_append(m_headers, "Content-Type: application/json");//by tang
      //m_headers = curl_slist_append(m_headers,"Accept: application/json-rpc");
	  m_headers = curl_slist_append(m_headers, "Accept: application/json");
      if((error = (size_t)curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, m_headers))!=0)
      {
        std::cerr << curl_easy_strerror((CURLcode)error);
        // TODO maybe throw exception here
        return;
      }

      /* Force curl to use POST */
      if((error = (size_t)curl_easy_setopt(m_curlHandle, CURLOPT_POST, 
              1)) != 0)
      {
        std::cerr << curl_easy_strerror((CURLcode)error);
        // TODO maybe throw exception here
        return;
      }

      // test if mutex is valid
      if(!m_mutex.Lock())// if(m_mutex.Lock())
      {
        std::cerr << "Can't create mutex for receive list.\n";
        // TODO maybe throw exception here
        return;
      }
      m_mutex.Unlock();

      this->SetRecvListSize(-1); 
      this->SetAddress("");
      this->SetPort(0);
      this->SetEncapsulatedFormat(Json::Rpc::RAW);
    }

	RestfulHttpClient::~RestfulHttpClient()
    {
      /* Clean up after we are done */
      curl_slist_free_all(m_headers);
      curl_global_cleanup();
    }

    void RestfulHttpClient::ChangeAddress(const std::string& address)
    {
      this->SetAddress(address);
    }

    void RestfulHttpClient::ChangePort(uint16_t port)
    {
      this->SetPort(port);

      size_t error = 0;
      if((error = (size_t)curl_easy_setopt(m_curlHandle, CURLOPT_PORT, 
              (long)port)) != 0)
      {
        std::cerr << curl_easy_strerror((CURLcode)error);
        // TODO maybe throw exception here
        return;
      }
    }

	void RestfulHttpClient::ChangeUserpwd(const std::string& username, const std::string& password)
	{
		std::string userpwd = username + ":" + password;
		curl_easy_setopt(m_curlHandle, CURLOPT_USERPWD,  userpwd.c_str());
	}

    ssize_t RestfulHttpClient::Send(const std::string& data)
    {
      if(this->GetAddress() == "")
      {
        return -1;
      }

      std::string rep = data;

      /* encoding if any */
      if(GetEncapsulatedFormat() == Json::Rpc::NETSTRING)
      {
        rep = netstring::encode(rep);
      }
#ifdef DEBUG
      curl_easy_setopt(m_curlHandle, CURLOPT_VERBOSE);
#endif

      size_t error = 0;
      /* Tell curl which address to use */
      if((error = (size_t)curl_easy_setopt(m_curlHandle, CURLOPT_URL, 
              this->GetAddress().c_str())) != 0)
      {
        std::cerr << curl_easy_strerror((CURLcode)error);
        return error;
      }

      /* Tell curl what data to send */
      if((error = (size_t)curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, 
              data.c_str())) != 0)
      {
        std::cerr << curl_easy_strerror((CURLcode)error);
        return error;
      }
      if((error = (size_t)curl_easy_setopt(m_curlHandle, 
              CURLOPT_POSTFIELDSIZE,
              data.length())) != 0)
      {
        std::cerr << curl_easy_strerror((CURLcode)error);
        return error;
      }

      /* Send the data */
      if((error = (size_t)curl_easy_perform(m_curlHandle)) != 0)
      { 
        std::cerr << curl_easy_strerror((CURLcode)error);
        return error;
      }
      return 0;
    }

	/*
	接收数据回调函数
	size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
	https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
	注意：每次接收的数据长度并不一定是一个完整的包，需要自己判断处理
	*/
    size_t RestfulHttpClient::StaticRecv(void *buffer, size_t size, size_t nmemb, void* userdata)
    {
      /* Get the data without the null term char */
      std::string data;
      data.assign((char*)buffer, (size_t)0, size*nmemb);

	  std::cout << "StaticRecv:" << std::endl;
	  std::cout << data << std::endl;

      /* Send our data to the current class instance */
      static_cast<RestfulHttpClient*>(userdata)->StoreRecv(data);

      /* return the size of data received */
      return size*nmemb;
    }

    void RestfulHttpClient::StoreRecv(const std::string &data)
    {
      m_mutex.Lock();
      this->m_lastReceivedList.push_back(data);
      if(m_lastReceivedListSize == -1)//初始化的值为-1
      {
        m_mutex.Unlock();
        return;
      }
	  /*
      if(m_lastReceivedList.size() > (unsigned int)m_lastReceivedListSize)
      {
        m_lastReceivedList.pop_front();//? why
      }*/
      m_mutex.Unlock();
    }

	//应该是接收一个完整的数据包，根据具体的应用去判断是否是完整的数据包
	//本应用中返回值为json，应该为{}包含的内容，初步以此判断
    ssize_t RestfulHttpClient::Recv(std::string &data)
    {
      if(m_lastReceivedList.empty())
      {
        return 0;
      }

      m_mutex.Lock();
      data = this->m_lastReceivedList.front();
      ssize_t length = this->m_lastReceivedList.front().length();
      this->m_lastReceivedList.pop_front();
	  //by tang
	  while (data[data.length() - 1] != '}' && !m_lastReceivedList.empty())//判断json字条串是否结束
	  {
		  data += this->m_lastReceivedList.front();
		  length += this->m_lastReceivedList.front().length();
		  this->m_lastReceivedList.pop_front();
	  }
      m_mutex.Unlock();

      return length;
    }

    ssize_t RestfulHttpClient::WaitRecv(std::string &data)
    {
      while(data.size() == 0)
      {
        this->Recv(data);
      }
      return data.size();
    }

    ssize_t RestfulHttpClient::WaitRecv(std::string &data, unsigned int timeout)
    {
      time_t cur_time = time(NULL);
      while(data.size() == 0 && 
          static_cast<time_t>(cur_time + timeout) > time(NULL))
      {
        this->Recv(data);
      }
      return data.size();
    }

    void RestfulHttpClient::SetRecvListSize(int size)
    {
      if(size < -1)
      {
        size = -1;
      }
      this->m_lastReceivedListSize = size;
    }

    int RestfulHttpClient::GetRecvListSize()
    {
      return this->m_lastReceivedListSize;
    }

    /* We don't need these functions */
    bool RestfulHttpClient::Connect()
    {
      return false;
    }

    void RestfulHttpClient::Close()
    {
    }

    int RestfulHttpClient::GetSocket() const
    {
      return -1; 
    }
  } /* namespace Rpc */
} /* namespace Json */

