#include "server_http.hpp"
#include "client_http.hpp"

//Added for the json-example
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>


//Added for the default_resource example
#include<fstream>

using namespace std;
//Added for the json-example:
using namespace boost::property_tree;

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;
typedef SimpleWeb::Client<SimpleWeb::HTTP> HttpClient;


template<class Response>
bool send_file(Response& response, const std::string& root_dir, const std::string& http_path, int status=200) {
    boost::system::error_code ec;
    boost::filesystem::path real_path = boost::filesystem::canonical(boost::filesystem::path(root_dir), ec);
    if(ec)
    {
        std::cerr << "Invalid web root directory \"" << root_dir << "\" : " << ec.message() << std::endl;
        return false;
    }

    boost::filesystem::path req_path(http_path);
    for(auto it=req_path.begin(); it!=req_path.end();++it) {
        if(*it=="." || *it=="/")
            continue;
        if(*it=="..") {
            std::cerr << "\"..\" not allowed in path " << req_path << std::endl;
            return false;
        }
        real_path/=*it;
        if(!boost::filesystem::exists(real_path)) {
            std::cerr << "Invalid path " << real_path << std::endl;
            return false;
        }
        if(boost::filesystem::is_symlink(real_path)) {
            std::cerr << "Symlink " << real_path << " not allowed" << std::endl;
            return false;
        }
    }

    if(!boost::filesystem::is_regular_file(real_path)) {
        std::cerr << real_path << " is not a file" << std::endl;
        return false;
    }

    boost::filesystem::ifstream ifs(real_path, std::ios::binary);
    if(!ifs) {
        std::cerr << "Failed opening file " << real_path << std::endl;
        return false;
    }

    static std::unordered_map<std::string, std::string> content_types= {
        {".txt", "Content-Type: text/plain\r\n"},
        {".png", "Content-Type: image/png\r\n"},
        {".jpg", "Content-Type: image/jpeg\r\n"},
        {".jpeg", "Content-Type: image/jpeg\r\n"},
        {".gif", "Content-Type: image/gif\r\n"},
        {".css", "Content-Type: text/css\r\n"},
        {".html", "Content-Type: text/html\r\n"},
        {".pdf", "Content-Type: application/pdf\r\n"},
        {".json", "Content-Type: application/json\r\n"}
    };

    std::string content_type;
    const auto it=content_types.find(boost::filesystem::extension(real_path));
    if(it!=content_types.end())
        content_type=it->second;
    size_t length=boost::filesystem::file_size(real_path);
    response << "HTTP/1.1 " << status << "\r\n" << content_type << "Content-Length: " << length << "\r\n\r\n";

    //read and send 128 KB at a time if file-size>buffer_size
    size_t buffer_size=131072;
    if(length>buffer_size) {
        std::vector<char> buffer(buffer_size);
        size_t read_length;
        while((read_length=ifs.read(&buffer[0], buffer_size).gcount())>0) {
            response.stream.write(&buffer[0], read_length);
            response.flush();
        }
    }
    else
        response << ifs.rdbuf();
    return true;
}


int main() {
    //HTTP-server at port 8080 using 4 threads
    HttpServer server(8080, 4);
    
    //Add resources using path-regex and method-string, and an anonymous function
    //POST-example for the path /string, responds the posted string
    server.resource["^/string$"]["POST"]=[](HttpServer::Response& response, shared_ptr<HttpServer::Request> request) {
        //Retrieve string from istream (request->content)
        stringstream ss;
        request->content >> ss.rdbuf();
        string content=ss.str();
        
        response << "HTTP/1.1 200 OK\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
    };
    
    //POST-example for the path /json, responds firstName+" "+lastName from the posted json
    //Responds with an appropriate error message if the posted json is not valid, or if firstName or lastName is missing
    //Example posted json:
    //{
    //  "firstName": "John",
    //  "lastName": "Smith",
    //  "age": 25
    //}
    server.resource["^/json$"]["POST"]=[](HttpServer::Response& response, shared_ptr<HttpServer::Request> request) {
        try {
            ptree pt;
            read_json(request->content, pt);

            string name=pt.get<string>("firstName")+" "+pt.get<string>("lastName");

            response << "HTTP/1.1 200 OK\r\nContent-Length: " << name.length() << "\r\n\r\n" << name;
        }
        catch(exception& e) {
            response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
        }
    };
    
    //GET-example for the path /info
    //Responds with request-information
    server.resource["^/info$"]["GET"]=[](HttpServer::Response& response, shared_ptr<HttpServer::Request> request) {
        stringstream content_stream;
        content_stream << "<h1>Request from " << request->endpoint.address().to_string() << " (" << request->endpoint.port() << ")</h1>";
        content_stream << request->method << " " << request->path << " HTTP/" << request->http_version << "<br>";
        for(auto& header: request->header) {
            content_stream << header.first << ": " << header.second << "<br>";
        }
        
        //find length of content_stream (length received using content_stream.tellp())
        content_stream.seekp(0, ios::end);
        
        response <<  "HTTP/1.1 200 OK\r\nContent-Length: " << content_stream.tellp() << "\r\n\r\n" << content_stream.rdbuf();
    };
    
    //GET-example for the path /match/[number], responds with the matched string in path (number)
    //For instance a request GET /match/123 will receive: 123
    server.resource["^/match/([0-9]+)$"]["GET"]=[](HttpServer::Response& response, shared_ptr<HttpServer::Request> request) {
        string number=request->path_match[1];
        response << "HTTP/1.1 200 OK\r\nContent-Length: " << number.length() << "\r\n\r\n" << number;
    };
    
    //Default GET-example. If no other matches, this anonymous function will be called. 
    //Will respond with content in the web/-directory, and its subdirectories.
    //Default file: index.html
    //Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
    server.default_resource["GET"]=[](HttpServer::Response& response, shared_ptr<HttpServer::Request> request) {
        string http_path=(request->path=="/") ? "/index.html":request->path;
        if(!send_file(response, "web", http_path) && !send_file(response, "web", "/404.html", 404)) {
            std::string content="\"" + http_path + "\" not found";
            response << "HTTP/1.1 404 Not found\r\nContent-type: text/plain\r\nContent-Length: " <<
                        content.length() << "\r\n\r\n" << content;
        }
    };
    
    thread server_thread([&server](){
        //Start server
        server.start();
    });
    
    //Wait for server to start so that the client can connect
    this_thread::sleep_for(chrono::seconds(1));
    
    //Client examples
    HttpClient client("localhost:8080");
    auto r1=client.request("GET", "/match/123");
    cout << r1->content.rdbuf() << endl;

    string json="{\"firstName\": \"John\",\"lastName\": \"Smith\",\"age\": 25}";
    stringstream ss(json);    
    auto r2=client.request("POST", "/string", ss);
    cout << r2->content.rdbuf() << endl;
    
    ss.str(json);
    auto r3=client.request("POST", "/json", ss);
    cout << r3->content.rdbuf() << endl;


    server_thread.join();
    
    return 0;
}
