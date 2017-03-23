
#include <string>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>

#include <boost/filesystem.hpp>

#include "nlohmann/json.hpp"

#include "simple-web-server/client_http.hpp"
#include "simple-websocket-server/client_ws.hpp"

#include "ScreepsApi/ApiManager.hpp"
#include "ScreepsApi/Web.hpp"

#include "ProgramApi/ArgumentParser.hpp"

std::string toString ( std::istream& stream )
{
    /**/
    std::string ret;
    char buffer[4096];
    while (stream.read(buffer, sizeof(buffer)))
        ret.append(buffer, sizeof(buffer));
    ret.append(buffer, stream.gcount());
    return ret;
}

/*
 *
 * encapsulation of Web::Client inside a ScreepsApi::Web::Client
 *
 */

class WebClient : public ScreepsApi::Web::Client
{
public:
    WebClient ( std::string host_port ) : m_web ( host_port ) {}
    virtual void connect ()
    {
        m_web.connect ();
    }
    virtual void close()
    {
        m_web.close ();
    }
    virtual ScreepsApi::Web::Reply request ( ScreepsApi::Web::RoutingMethod method, std::string uri, std::string content = "", ScreepsApi::Web::Header header = ScreepsApi::Web::Header () )
    {
        ScreepsApi::Web::Reply out;
        std::string meth = "";
        switch ( method )
        {
        case ScreepsApi::Web::RoutingMethod::HttpGet:
            meth = "GET";
            break;
        case ScreepsApi::Web::RoutingMethod::HttpPost:
            meth = "POST";
            break;
        default:
            break;
        }
        if ( meth != "" )
        {
            std::shared_ptr<SimpleWeb::Client<SimpleWeb::HTTP>::Response> reply = m_web.request ( meth, uri, content, header.m_data );
            for(auto it = reply->header.begin(); it != reply->header.end(); it++) {
                out.m_header.m_data[it->first] = it->second;
            }
            out.m_content = toString ( reply->content );
        }
        return out;
    }
protected:
    SimpleWeb::Client<SimpleWeb::HTTP> m_web;
};

class WebsocketClient : public ScreepsApi::Web::Socket
{
public:
    WebsocketClient ( std::string host_port_path ) : m_socket ( host_port_path ) {
        m_socket.on_open = std::bind(&WebsocketClient::on_open,this);
        m_socket.on_message = std::bind(&WebsocketClient::on_message,this,std::placeholders::_1);
        m_socket.on_close = std::bind(&WebsocketClient::on_close,this,std::placeholders::_1,std::placeholders::_2);
        m_socket.on_error = std::bind(&WebsocketClient::on_error,this,std::placeholders::_1);
    }
    virtual void connect ()
    {
        m_socketThreadQuit = false;
        m_socketThread = std::thread ( &WebsocketClient::thread_loop, this);
    }
    virtual void close()
    {
        m_socket.stop ();
        m_socketThread.join ();
    }
    virtual void send ( std::string message )
    {
        auto send_stream=std::make_shared<SimpleWeb::SocketClient<SimpleWeb::WS>::SendStream>();
        *send_stream << message;
        m_socket.send(send_stream);
    }
    virtual void subscribe ( std::string message, std::function<void(std::string)> callback )
    {
        m_messageHandlers [ message ] = callback;
    }
    virtual void unsubscribe ( std::string message )
    {
        m_messageHandlers.erase ( m_messageHandlers.find ( message ) );
    }
protected:
    SimpleWeb::SocketClient<SimpleWeb::WS> m_socket;
    std::thread m_socketThread;
    bool m_socketThreadQuit;
    std::map < std::string, std::function<void(std::string)> > m_messageHandlers;
    void thread_loop()
    {
        m_socket.start ();
    }
    void on_open()
    {
    }
    void on_message(std::shared_ptr<SimpleWeb::SocketClient<SimpleWeb::WS>::Message> message)
    {
        std::string key = "";
        auto msg = message->string ();
        if (msg.substr(0,2) == "[\"" )
        {
            /*
                ["id",data]
            */
            std::string tmp = msg.substr ( 2, msg.length () - 3 );
            size_t pos = tmp.find_first_of ( "\"" );
            std::string key = tmp.substr ( 0, pos );
            std::string value = tmp.substr ( pos + 2 );
            if ( m_messageHandlers.find ( key ) != m_messageHandlers.end () )
            {
                m_messageHandlers[key] ( value );
            }
            return;
        }
        for ( auto it : m_messageHandlers )
        {
            if ( msg.substr ( 0, it.first.length () ) == it.first )
            {
                it.second ( msg );
                return;
            }
        }
        std::cout << "Socket message :: " << msg.substr ( 0, 40 ) << std::endl;
    }
    void on_close(int, const std::string&)
    {
        std::cout << "Socket closed" << std::endl;
    }
    void on_error(const boost::system::error_code&)
    {
        std::cout << "Socket error" << std::endl;
    }
};

/*
 *
 * encapsulation of Web::Client inside a ScreepsApi::Web::Client
 *
 */

void error ( std::string message )
{
    std::cout << "Error: " << message << std::endl;
    exit ( -1 );
}

nlohmann::json gServerOptions = {
        { "serverIP", {
            { "short", "s" },
            { "long", "server" },
            { "type", "string" },
            { "optional", true },
            { "help", "hostname / hostip of the private screeps server" },
            {"value", {
                { "default", "localhost" },
                { "required", true }
            } }
        } },
        { "serverPort", {
            { "short", "p" },
            { "long", "port" },
            { "type", "int" },
            { "optional", true },
            { "help", "port opened on the private screeps server" },
            { "value", {
                { "default", "21025" },
                { "required", true }
            } }
        } },
        { "username", {
            { "short", "u" },
            { "long", "username" },
            { "type", "string" },
            { "optional", false },
            { "help", "username on the server" },
            { "value", {
                { "required", true }
            } }
        } },
        { "password", {
            { "short", "w" },
            { "long", "password" },
            { "type", "string" },
            { "optional", false },
            { "help", "paswword for the account on the server" },
            { "value", {
                { "required", true }
            } }
        } },
        { "room", {
            { "short", "r" },
            { "long", "room" },
            { "type", "string" },
            { "optional", false },
            { "help", "room Id to observe : WxNy" },
            {"value", {
                { "required", true }
            } }
        } }
    };

class ServerOptions : public ProgramApi::ArgumentParser
{
public:
    ServerOptions () : ProgramApi::ArgumentParser (gServerOptions)
    {
    }
};

nlohmann::json initialRoomData;
nlohmann::json updatedRoomData;

void roomProcess(std::string consoleData)
{
    updatedRoomData = nlohmann::json::parse ( consoleData );
}

int main ( int argc, char** argv )
{
    int index = 1;
    ServerOptions server;
    ProgramApi::ArgumentParser::Arguments serverOptions = server.parseArgs ( index, argc, argv );

    std::shared_ptr < ScreepsApi::Web::Client > web (
        new WebClient ( serverOptions["serverIP"].get<std::string>()+":"+serverOptions["serverPort"].get<std::string>() )
    );
    std::shared_ptr < ScreepsApi::Web::Socket > socket (
        new WebsocketClient ( serverOptions["serverIP"].get<std::string>()+":"+serverOptions["serverPort"].get<std::string>()+"/socket/websocket" )
    );
    ScreepsApi::ApiManager::Instance ().initialize ( web, socket );
    std::shared_ptr < ScreepsApi::Api > client = ScreepsApi::ApiManager::Instance ().getApi ();
    bool ok = client->Signin ( serverOptions["username"], serverOptions["password"] );
    if ( ! ok ) {
        std::cerr << "Error: cannot connect/signin to the server" << std::endl;
        exit ( -1 );
    }
    while ( ! client->initialized () )
        std::this_thread::sleep_for ( std::chrono::milliseconds ( 5 ) );
    std::cout << "signed in " << ok << std::endl;
    initialRoomData = client->Room (serverOptions["room"].get<std::string>());
    client->RoomListener ( serverOptions["room"].get<std::string>(), roomProcess );
    while ( true )
        std::this_thread::sleep_for ( std::chrono::milliseconds ( 5 ) );
    return 0;
}
