
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

#define NCURSES_WIDECHAR 1
#include <curses.h>

ProgramApi::ArgumentParser::Arguments serverOptions;
std::shared_ptr < ScreepsApi::Api > client;

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
class Exception: public std::exception
{
public:
    /** Constructor (C strings).
     *  @param message C-style string error message.
     *                 The string contents are copied upon construction.
     *                 Hence, responsibility for deleting the char* lies
     *                 with the caller. 
     */
    explicit Exception(const char* message):
      msg_(message)
      {
      }

    /** Constructor (C++ STL strings).
     *  @param message The error message.
     */
    explicit Exception(const std::string& message):
      msg_(message)
      {}

    /** Destructor.
     * Virtual to allow for subclassing.
     */
    virtual ~Exception() throw (){}

    /** Returns a pointer to the (constant) error description.
     *  @return A pointer to a const char*. The underlying memory
     *          is in posession of the Exception object. Callers must
     *          not attempt to free the memory.
     */
    virtual const char* what() const throw (){
       return msg_.c_str();
    }

protected:
    /** Error message.
     */
    std::string msg_;
};
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
        if ( m_messageHandlers.find ( message ) != m_messageHandlers.end () )
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
    }
    void on_close(int, const std::string&)
    {
    }
    void on_error(const boost::system::error_code&)
    {
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
        } },
        { "disableGUI", {
            { "short", "g" },
            { "long", "gui" },
            { "type", "bool" },
            { "optional", true },
            { "help", "de-activate gui display" },
            {"value", {
                { "default", false }
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

typedef unsigned long long GameTime;
typedef char GameObjectName[100];
typedef char GameObjectId[20];
typedef char MineralType[10];

typedef struct { int level, progress; GameTime downgradeTime; } Controller;
typedef struct { int energy, energyCapacity; int invaderHarvested; int ticksToRegeneration; GameTime nextRegenerationTime; } Source;
typedef struct { int density; float amount; MineralType type; } Mineral;

typedef struct { int energy, energyCapacity; bool off; GameObjectId spawning; GameObjectName name; } Spawn;
typedef struct { int energy, energyCapacity; bool off; } Extension;

typedef struct { GameTime nextDecayTime; } Road;

typedef struct { int energy, energyCapacity; } Container;
typedef struct { int energy, energyCapacity; } Storage;
typedef struct { GameObjectId attack, heal, repair; int energy, energyCapacity; } Tower;
typedef struct {} Extractor;

typedef struct {bool spawning; } Creep;

typedef union {
    Controller controller;
    Source source;
    Mineral mineral;
    Spawn spawn;
    Extension extension;
    Road road;
    Container container;
    Storage storage;
    Tower tower;
    Creep creep;
} GameObjectData;

typedef struct {
    std::string id;
    std::string type;
    int x, y;
    int hits, hitsMax;
    std::string user;
    GameObjectData data;
    //
    bool staticObject;
} GameObject;

void ControllerFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "level") != data.end () ) go.data.controller.level = data["level"].get<int> ();
    if ( data.find ( "progress") != data.end () ) go.data.controller.progress = data["progress"].get<int> ();
    if ( data.find ( "downgradeTime") != data.end () ) go.data.controller.downgradeTime = data["downgradeTime"].get<int> ();
}

void SourceFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "energy") != data.end () ) go.data.source.energy = data["energy"].get<int> ();
    if ( data.find ( "energyCapacity") != data.end () ) go.data.source.energyCapacity = data["energyCapacity"].get<int> ();
    if ( data.find ( "invaderHarvested") != data.end () ) go.data.source.invaderHarvested = data["invaderHarvested"].get<int> ();
    if ( data.find ( "ticksToRegeneration") != data.end () ) go.data.source.ticksToRegeneration = data["ticksToRegeneration"].get<int> ();
    if ( data.find ( "nextRegenerationTime") != data.end () ) go.data.source.nextRegenerationTime = data["nextRegenerationTime"].get<int> ();
}

void MineralFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "density") != data.end () ) go.data.mineral.density = data["density"].get<int> ();
    if ( data.find ( "amount") != data.end () ) go.data.mineral.amount = data["amount"].get<float> ();
    if ( data.find ( "type") != data.end () ) sprintf ( go.data.mineral.type, "%s", data["type"].get<std::string> ().c_str () );
}

void SpawnFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "energy") != data.end () ) go.data.spawn.energy = data["energy"].get<int> ();
    if ( data.find ( "energyCapacity") != data.end () ) go.data.spawn.energyCapacity = data["energyCapacity"].get<int> ();
    if ( data.find ( "off") != data.end () ) go.data.spawn.off = data["off"].get<bool> ();
    if ( data.find ( "name") != data.end () ) sprintf ( go.data.spawn.name, "%s", data["name"].get<std::string> ().c_str () );
    if ( data.find ( "spawning") != data.end () ) {
        if ( ! data["spawning"].is_null () ) 
            if ( data["spawning"].find ( "_id") != data["spawning"].end () )
                sprintf ( go.data.spawn.spawning, "%s", data["spawning"]["_id"].get<std::string> ().c_str () );
    }
}

void ExtensionFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "energy") != data.end () ) go.data.spawn.energy = data["energy"].get<int> ();
    if ( data.find ( "energyCapacity") != data.end () ) go.data.spawn.energyCapacity = data["energyCapacity"].get<int> ();
    if ( data.find ( "off") != data.end () ) go.data.spawn.off = data["off"].get<bool> ();
}

void RoadFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "nextDecayTime") != data.end () ) go.data.road.nextDecayTime = data["nextDecayTime"].get<int> ();
}

void ContainerFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "energy") != data.end () ) go.data.container.energy = data["energy"].get<int> ();
    if ( data.find ( "energyCapacity") != data.end () ) go.data.container.energyCapacity = data["energyCapacity"].get<int> ();
}

void StorageFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "energy") != data.end () ) go.data.storage.energy = data["energy"].get<int> ();
    if ( data.find ( "energyCapacity") != data.end () ) go.data.storage.energyCapacity = data["energyCapacity"].get<int> ();
}

void TowerFromJson ( GameObject& go, nlohmann::json data )
{
    if ( data.find ( "energy") != data.end () ) go.data.tower.energy = data["energy"].get<int> ();
    if ( data.find ( "energyCapacity") != data.end () ) go.data.tower.energyCapacity = data["energyCapacity"].get<int> ();
    if ( data.find ( "attack") != data.end () ) {
        if ( ! data["attack"].is_null () ) 
            if ( data["attack"].find ( "_id") != data["attack"].end () )
                sprintf ( go.data.tower.attack, "%s", data["attack"]["_id"].get<std::string> ().c_str () );
    }
    if ( data.find ( "heal") != data.end () ) {
        if ( ! data["heal"].is_null () ) 
            if ( data["heal"].find ( "_id") != data["heal"].end () )
                sprintf ( go.data.tower.heal, "%s", data["heal"]["_id"].get<std::string> ().c_str () );
    }
    if ( data.find ( "repair") != data.end () ) {
        if ( ! data["repair"].is_null () ) 
            if ( data["repair"].find ( "_id") != data["repair"].end () )
                sprintf ( go.data.tower.repair, "%s", data["repair"]["_id"].get<std::string> ().c_str () );
    }
}

void CreepFromJson ( GameObject& go, nlohmann::json data )
{
    go.staticObject = false;
    if ( data.find ( "spawning") != data.end () ) go.data.creep.spawning = data["spawning"].get<bool> ();
}

void FromJson ( GameObject& go, nlohmann::json data )
{
    go.staticObject = true;
    if ( data.find ( "_id" ) != data.end () ) go.id = data["_id"].get<std::string>();
    if ( data.find ( "user" ) != data.end () ) go.user = data["user"].get<std::string>();
    if ( data.find ( "type" ) != data.end () ) go.type = data["type"].get<std::string>();
    if ( data.find ( "x" ) != data.end () ) go.x = data["x"].get<int>();
    if ( data.find ( "y" ) != data.end () ) go.y = data["y"].get<int>();
    if ( data.find ( "hitsMax" ) != data.end () ) go.hitsMax = data["hitsMax"].get<int>();
    if ( data.find ( "hits" ) != data.end () ) go.hits = data["hits"].get<int>();
    if ( go.type == "controller" ) ControllerFromJson ( go, data );
    if ( go.type == "source" ) SourceFromJson ( go, data );
    if ( go.type == "mineral" ) MineralFromJson ( go, data );
    if ( go.type == "spawn" ) SpawnFromJson ( go, data );
    if ( go.type == "extension" ) ExtensionFromJson ( go, data );
    if ( go.type == "road" ) RoadFromJson ( go, data );
    if ( go.type == "container" ) ContainerFromJson ( go, data );
    if ( go.type == "storage" ) StorageFromJson ( go, data );
    if ( go.type == "creep" ) CreepFromJson ( go, data );
}

std::map < std::string, GameObject > roomContent;

nlohmann::json userData;
nlohmann::json initialRoomData;
nlohmann::json updatedRoomData;
bool roomInitialized = false;
bool updatePaused = false;

void drawWindow ();

std::map < std::string, bool > displayed;

void initializeRoomContent (nlohmann::json roomData)
{
    if ( roomData.is_null () ) throw Exception ( "null data received" );
    std::cout << roomData.dump ( 4 ) << std::endl;
    for ( nlohmann::json::iterator it = roomData["objects"].begin () ; it != roomData["objects"].end () ; ++ it )
    {
        GameObject go;
        try {
            FromJson ( go, it.value () );
        }
        catch (...) { throw Exception ( "problem in room initial content" ); }
        roomContent[go.id] = go;
        /**/
        if ( serverOptions["disableGUI"].get<bool> () ) if ( displayed.find ( go.type ) == displayed.end () )
        {
            std::cout << "---------" << go.type << "---------" << std::endl << it.value().dump () << std::endl;
            displayed[go.type] = true;
        }
        /**/
    }
    roomInitialized = true;
}

bool firstUpdate = true;

void updateRoomContent (nlohmann::json roomData)
{
    //if ( firstUpdate )
    for ( nlohmann::json::iterator it = roomData["objects"].begin () ; it != roomData["objects"].end () ; ++ it )
    {
        std::string id = it.key (); // sprintf ( id, "%s", it.key ().c_str () );
        GameObject go = roomContent[id];
        try {
            if ( it.value().find ( "x" ) != it.value().end () ) go.x = it.value()["x"].get<int> ();
            if ( it.value().find ( "y" ) != it.value().end () ) go.y = it.value()["y"].get<int> ();
        }
        catch (...) { throw Exception ( "problem in room updated content" ); }
        roomContent[id] = go;
        /**/
        if ( serverOptions["disableGUI"].get<bool> () ) if ( displayed.find ( go.type ) == displayed.end () )
        {
            std::cout << "---------" << go.type << "---------" << std::endl << it.value().dump () << std::endl;
            displayed[go.type] = true;
        }
        /**/
    }
}

void roomProcess(std::string consoleData)
{
    static bool first = true;
    updatedRoomData = nlohmann::json::parse ( consoleData );
    if ( first ) {
        initializeRoomContent(updatedRoomData);
        first = false;
        std::cout << std::endl;
        displayed.clear ();
    }
    else if ( ! updatePaused ) {
        updateRoomContent(updatedRoomData);
    }
    if ( ! serverOptions["disableGUI"].get<bool> () )
        drawWindow ();
}

/*
    Ncurses UI
 */

typedef struct {
  int code; char *desc;
} colour;

colour colours[] = {
    { -1,            "default" },
    { COLOR_BLACK,   "BLACK" },
    { COLOR_RED,     "RED" },
    { COLOR_GREEN,   "GREEN" },
    { COLOR_YELLOW,  "YELLOW" },
    { COLOR_BLUE,    "BLUE" },
    { COLOR_MAGENTA, "MAGENTA" },
    { COLOR_CYAN,    "CYAN" },
    { COLOR_WHITE,   "WHITE" },
};

int WIDTH, HEIGHT;
int x, y, w, h;
WINDOW* window;
int mouse_x, mouse_y;
std::string terrain = "";

std::vector < GameObject > underMouse;

void initScreen ()
{
    setlocale(LC_ALL, "");
    initscr();
    if(has_colors() == FALSE)
    {	endwin();
        printf("Your terminal does not support color\n");
        exit(1);
    }
    start_color();
    use_default_colors();
    mousemask(ALL_MOUSE_EVENTS, NULL);

    int i,j;
    int ccount = 9;
    init_color(COLOR_BLACK, 0, 0, 0);
    init_color(COLOR_RED, 1000, 0, 0);
    init_color(COLOR_GREEN, 0, 1000, 0);
    init_color(COLOR_YELLOW, 1000, 1000, 0);
    init_color(COLOR_BLUE, 0, 0, 1000);
    init_color(COLOR_MAGENTA, 1000, 0, 1000);
    init_color(COLOR_CYAN, 0, 1000, 1000);
    init_color(COLOR_WHITE, 500, 500, 500);
    for(i=0; i<ccount; i++) { for (j=0;j<ccount;j++) {
        init_pair(i*ccount+j+1, colours[i].code, colours[j].code);
    } }
    
    HEIGHT = LINES;
    WIDTH = COLS >= 222 ? 222 : COLS;
}

void resetScreen ()
{
    endwin();
}

void initWindow ()
{
    window = subwin(stdscr, h, w, y, x);
    box(window, ACS_VLINE, ACS_HLINE);
}

void resetWindow ()
{
    free(window);
}

void print ( int x, int y, std::string str )
{
    mvaddstr ( y, x,str.c_str () );
}

void print ( int x, int y, std::wstring str )
{
    mvaddwstr ( y, x,str.c_str () );
}

char getMapAt ( std::string terrain, int x, int y )
{
    return terrain [ y * 50 + x ];
}

void getColor(char mcase, int& fg, int& bg, int x, int y)
{
    /*if ( x < 9 && y < 9 ) { fg = x; bg = y; }
    else*/ if ( mcase == '0' ) { fg = 8; bg = 8; }
    else if ( mcase == '1' ) { fg = 1; bg = 1; }
    else if ( mcase == '2' ) { fg = 4; bg = 4; }
    else { fg = 3; bg = 3; }
}

void drawWindow ()
{
    std::string terrain = initialRoomData["terrain"].get<std::string> ();
    int y = 0;
    std::string::iterator it = terrain.begin ();
    while ( it != terrain.end () )
    {
        std::string line = terrain.substr ( 50*(y), 50 );
        std::string::iterator itx = it;
        int x = 0;
        while ( itx != it + 50 )
        {
            char mcase = getMapAt(terrain, x, y );
            int fg, bg;
            getColor ( mcase, fg, bg, x, y );
            attron ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
            /*if ( x < 9 && y < 9 ) print ( 2*x+1, y+1, "XX" );
            else*/ print ( 2*x+1, y+1, "  " );
            attroff ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
            ++ itx;
            x ++;
        }
        it += 50;
        y ++;
    }
    std::map < std::string, GameObject >::const_iterator obj;
    for ( obj = roomContent.begin () ; obj != roomContent.end () ; ++ obj )
    {
        int x = obj->second.x, y = obj->second.y;
        if ( ! obj->second.staticObject ) continue;
            char mcase = getMapAt(terrain, x, y );
            int fg, bg;
            getColor ( mcase, fg, bg, x, y ); fg = 4;
        std::wstring out;
        if ( obj->second.type == "controller" ) { wchar_t c = L'\u2775' + obj->second.data.controller.level; out = (out + c)+L" "; }
        if ( obj->second.type == "source" ) {
            wchar_t c = L'\u2666';
            if ( obj->second.data.source.energy == 0 ) c = L'\u2662';
            out = (out + c)+L" ";
        }
        if ( obj->second.type == "mineral" ) { wchar_t c = L'\u267D'; out = (out + c)+L" "; }

        if ( obj->second.type == "road" ) { wchar_t c = L'\u254B'; out = (out + c)+L" "; fg = 5; }

        if ( obj->second.type == "extension" ) {
            wchar_t c = L'\u29BF';
            if ( obj->second.data.extension.energy == 0 ) c = L'\u29BE';
            out = (out + c)+L" ";
        }
        if ( obj->second.type == "spawn" ) {
            wchar_t c = L'\u2617';
            if ( obj->second.data.spawn.energy == 0 ) c = L'\u2616';
            out = (out + c)+L" ";
        }
        
        if ( obj->second.type == "container" ) { wchar_t c = L'\u29EF'; out = (out + c)+L" "; }
        if ( obj->second.type == "storage" ) { wchar_t c = L'\u29F3'; out = (out + c)+L" "; }
        
        if ( obj->second.type == "tower" ) { wchar_t c = L'\u265C'; out = (out + c)+L" "; }
        if ( obj->second.type == "extractor" ) { wchar_t c = L'\u2622';/*267C';*/ out = (out + c)+L" "; }
            attron ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
            print ( 2*x+1, y+1, out );
            attroff ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
    }
    for ( obj = roomContent.begin () ; obj != roomContent.end () ; ++ obj )
    {
        int x = obj->second.x, y = obj->second.y;
        if ( obj->second.staticObject ) continue;
        if ( obj->second.data.creep.spawning ) continue;
            char mcase = getMapAt(terrain, x, y );
            int fg, bg;
            getColor ( mcase, fg, bg, x, y ); fg = 3;
            std::wstring out = L"\u265F";
            if ( obj->second.user != userData["_id"].get<std::string> () ) fg = 2;
            attron ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
            print ( 2*x+1, y+1, out );
            attroff ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
    }
    /**/
    std::ostringstream stream;
    stream << "Mouse: " << mouse_x << "," << mouse_y;
    std::string message = stream.str ();
    print ( 102, 1, message );
    std::wstring tmp = L"\u2673";
    print ( 102, 2, tmp );
    /*
    int yy = 4;
    for ( obj = roomContent.begin () ; obj != roomContent.end () ; ++ obj )
    {
        if ( obj->second.staticObject ) continue;
            stream << "Creep: " << obj->second.data.creep.name;
            message = stream.str ();
            print ( 102, yy, message );
            stream << "  " << obj->second.data.creep.name;
            message = stream.str ();
            print ( 102, yy, message );
            char mcase = getMapAt(terrain, x, y );
            int fg, bg;
            getColor ( mcase, fg, bg, x, y ); fg = 3;
            std::wstring out = L"\u265F";
            if ( obj->second.user != userData["_id"].get<std::string> () ) fg = 2;
            attron ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
            print ( 2*x+1, y+1, out );
            attroff ( COLOR_PAIR ( 1 + fg * 9 + bg ) );
            yy += 2;
    }
    */
    /**/
    wrefresh(window);
}

void selectUnderMouseObjects ( int x, int y )
{
    //if ( x > 50 || y > 50 ) return;
    mouse_x = x; mouse_y = y;
    //drawWindow ();
}

/*
*/

void my_handler(int s){
    if ( ! serverOptions["disableGUI"].get<bool> () )
    {
        resetWindow ();
        resetScreen ();
    }
    //std::cout << "caught signal" << std::endl;
    client->RoomListener ( serverOptions["room"].get<std::string>() );
    exit(1); 
}

int main ( int argc, char** argv )
{
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = my_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);

    try {
        int index = 1;
        ServerOptions server;
        serverOptions = server.parseArgs ( index, argc, argv );

        std::shared_ptr < ScreepsApi::Web::Client > web (
            new WebClient ( serverOptions["serverIP"].get<std::string>()+":"+serverOptions["serverPort"].get<std::string>() )
        );
        std::shared_ptr < ScreepsApi::Web::Socket > socket (
            new WebsocketClient ( serverOptions["serverIP"].get<std::string>()+":"+serverOptions["serverPort"].get<std::string>()+"/socket/websocket" )
        );
        ScreepsApi::ApiManager::Instance ().initialize ( web, socket );
        client = ScreepsApi::ApiManager::Instance ().getApi ();
        bool ok = client->Signin ( serverOptions["username"], serverOptions["password"] );
        if ( ! ok ) {
            std::cerr << "Error: cannot connect/signin to the server" << std::endl;
            exit ( -1 );
        }
        while ( ! client->initialized () ) std::this_thread::sleep_for ( std::chrono::milliseconds ( 5 ) );
    }
    catch ( ... )
    {
        exit ( -1 );
    }

    try {
        userData = client->User ();
        std::cout << userData.dump () << std::endl;
        initialRoomData = client->Room (serverOptions["room"].get<std::string>());
        client->RoomListener ( serverOptions["room"].get<std::string>(), roomProcess );
        while ( ! roomInitialized ) std::this_thread::sleep_for ( std::chrono::milliseconds ( 5 ) );
    }
    catch ( ... )
    {
        client->RoomListener ( serverOptions["room"].get<std::string>() );
        exit ( -1 );
    }

    try {
        if ( ! serverOptions["disableGUI"].get<bool> () )
        {
            x = y = 0; w = WIDTH; h = HEIGHT;
            initScreen ();
            initWindow ();
            while ( true )
            {
                drawWindow ();
                std::this_thread::sleep_for ( std::chrono::milliseconds ( 5 ) );
                MEVENT event;
                int key = getch ();
                if ( key == KEY_MOUSE && getmouse(&event) == OK )
                {
                    selectUnderMouseObjects ( event.x - 1, event.y - 1 );
                }
                if ( key == 'q' ) break;
                if ( key == 'p' ) updatePaused = ! updatePaused;
            }
            resetWindow ();
            resetScreen ();
        }
        else while ( true )
            std::this_thread::sleep_for ( std::chrono::milliseconds ( 5 ) );
    }
    catch ( ... )
    {
        resetWindow ();
        resetScreen ();
        client->RoomListener ( serverOptions["room"].get<std::string>() );
        exit ( -1 );
    }
    client->RoomListener ( serverOptions["room"].get<std::string>() );
    return 0;
}
