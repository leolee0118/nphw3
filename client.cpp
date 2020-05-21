#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <iostream>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/CreateBucketRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/Object.h>
#include <fstream>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

#define bufsize 256

using namespace std;
using namespace Aws;
using namespace Aws::S3;
using namespace Aws::S3::Model;
using namespace nlohmann;

string prefix = "0616220-";

struct user_data {
    int id, status;
    string name, email, password;
    void init() {
        id = -1;
        name = "";
        email = "";
        password = "";
        status = 0;
    }
};

user_data client_user;

// functions
void postForm(const char *req, vector<string> &cmd) {
    stringstream ss(req);
    string title, content, s;
    bool isTitle = false, isContent = false;

    while (ss >> s) {
        if (s == "--title") {
            isTitle = true;
            if (isContent) {
                if (content.size()) {
                    cmd.push_back(content);
                }
                content = "";
                isContent = false;
            }
            cmd.push_back(s);
            continue;
        } else if (s == "--content") {
            isContent = true;
            if (isTitle) {
                if (title.size()) {
                    cmd.push_back(title);
                }
                title = "";
                isTitle = false;
            }
            cmd.push_back(s);
            continue;
        }
        if (isTitle) {
            title = title + s + ' ';
            continue;
        }
        if (isContent) {
            content = content + s + ' ';
            continue;
        }
        cmd.push_back(s);
    }
    if (isTitle) {
        if (title.size()) {
            cmd.push_back(title);
        }
        title = "";
        isTitle = false;
    }
    if (isContent) {
        if (content.size()) {
            cmd.push_back(content);
        }
        content = "";
        isContent = false;
    }
}

void commentForm(const char *req, vector<string> &cmd) {
    stringstream ss(req);
    string s, sreq(req);
    int i = 0, len = 0;

    while (ss >> s) {
        cmd.push_back(s);
        len += s.size();
        len++;
        i++;
        if (i == 2) {
            break;
        }
    }
    s = sreq.substr(len, sreq.size() - len - 1);
    cmd.push_back(s);
}

void resForm(const char *res, int &cid, int &rid, string &sres) {
	string tsres(res);
	size_t fc = tsres.find(",");
	size_t sc = tsres.find(",", fc + 1);
	string scid = tsres.substr(0, fc), srid = tsres.substr(fc + 1, sc - fc - 1);
	sres = tsres.substr(sc + 1, tsres.size() - scid.size() - srid.size() - 2);
	cid = stoi(scid); rid = stoi(srid);
}

// aws
inline bool ifFileExist(const string& name)
{
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

bool createBucket(const String &bucketName, const BucketLocationConstraint &region = BucketLocationConstraint::us_east_1) {
    // Set up the request

    CreateBucketRequest request;
    request.SetBucket(bucketName);

    // Is the region other than us-east-1 (N. Virginia)?
    if (region != BucketLocationConstraint::us_east_1)
    {
        // Specify the region as a location constraint
        CreateBucketConfiguration bucket_config;
        bucket_config.SetLocationConstraint(region);
        request.SetCreateBucketConfiguration(bucket_config);
    }

    // Create the bucket
    S3Client s3_client;
    auto outcome = s3_client.CreateBucket(request);
    if (!outcome.IsSuccess())
    {
        auto err = outcome.GetError();
        cout << "ERROR: CreateBucket: " << 
            err.GetExceptionName() << ": " << err.GetMessage() << std::endl;
        return false;
    }
    return true;
}

bool putObject(const String& bucketName, const String& objectName, const string& fileName, const String& region = "") {
    // Verify file_name exists
    if (!ifFileExist(fileName)) {
        cout << "ERROR: NoSuchFile: The specified file does not exist\n";
        return false;
    }

    // If region is specified, use it
    Client::ClientConfiguration clientConfig;
    if (!region.empty())
        clientConfig.region = region;

    // Set up request
    // snippet-start:[s3.cpp.put_object.code]
    S3Client s3_client(clientConfig);
    PutObjectRequest objectRequest;

    objectRequest.SetBucket(bucketName);
    objectRequest.SetKey(objectName);
    const shared_ptr<IOStream> inputData = MakeShared<FStream>("SampleAllocationTag", fileName.c_str(), ios_base::in | ios_base::binary);
    objectRequest.SetBody(inputData);

    // Put the object
    auto put_object_outcome = s3_client.PutObject(objectRequest);
    if (!put_object_outcome.IsSuccess()) {
        auto error = put_object_outcome.GetError();
        cout << "ERROR: " << error.GetExceptionName() << ": " << error.GetMessage() << '\n';
        return false;
    }
    return true;
    // snippet-end:[s3.cpp.put_object.code]
}

bool getObject(const String& bucketName, const String& objectName, const string& fileName) {
	S3Client s3_client;
	GetObjectRequest objectRequest;

	objectRequest.SetBucket(bucketName);
	objectRequest.SetKey(objectName);
	auto get_object_outcome = s3_client.GetObject(objectRequest);
	if (get_object_outcome.IsSuccess()) {
		// Get an Aws::IOStream reference to the retrieved file
		auto &retrievedFile = get_object_outcome.GetResultWithOwnership().GetBody();

		// Alternatively, read the object's contents and write to a file
		ofstream outputFile(fileName, ios::binary);
		outputFile << retrievedFile.rdbuf();    
		return true;
	} else {
		auto error = get_object_outcome.GetError();
		std::cout << "ERROR: " << error.GetExceptionName() << ": " 
			<< error.GetMessage() << std::endl;
		return false;
	}
}

// handlers
bool register_handler(string name) {
	string bucketName = prefix + name;
	return createBucket(bucketName.c_str());
}

bool login_handler(string name) {
	client_user.init();
	client_user.name = name;
}

bool createPost_handler(string objectName, string content) {
	string bucketName = prefix + client_user.name;
	string fileName = "post/" + objectName;
	fstream file;
	file.open(fileName, ios::out);
	file << content;
	file.close();
	putObject(bucketName.c_str(), objectName.c_str(), fileName);
}

bool updatePost_handler() {
	
}

void middleware(const int cid, vector<string> &cmd) {
	if (cid == 0) { // register
		register_handler(cmd[1]);
	} else if (cid == 1) { // login
		login_handler(cmd[1]);
	} else if (cid == 7) { // create-post
		createPost_handler(cmd[3], cmd[5]);
	} else if (cid == 9) { // update-post
		getObject("0616220-yum", "yummy ", "test.txt");
	} else if (cid == 10) { // comment

	} else if (cid == 11) { // delete-post
		
	} else if (cid == 12) { // read

	}
}

void splitCommand(const char *req, vector<string> &cmd) {
	stringstream ss(req);
	string s, sreq(req);
	if  (sreq.substr(0, 11) == "create-post" || sreq.substr(0, 11) == "update-post") {
		postForm(req, cmd);
	} else if (sreq.substr(0, 7) == "comment") {
		commentForm(req, cmd);
	} else {
		while (ss >> s) {
			cmd.push_back(s);
		}
	}
}

int handler(const char *cres/*, vector<string> &cmd*/) { // 1: exit, 0: otherwise
	json jres = json::parse(cres);
	int cid = jres["cid"], rid = jres["rid"];
	string res = jres["msg"];
	vector<string> cmd = jres.at("cmd");


	// int cid, rid;
	// string res;
	// resForm(cres, cid, rid, res);
	if (cid == 4 && rid == 0) { // exit
		return 1;
	}
	if (rid == 0) {
		middleware(cid, cmd);
	}
	cout << res;
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		cerr << "No [hostname, port] provided\n";
		exit(1);
	}

    int sockfd, portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		cerr << "failed to open socket\n";
		exit(1);
	}

    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[2]);
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
		cerr << "failed to connect\n";
		exit(1);
	}
 
	Aws::SDKOptions options;
   	Aws::InitAPI(options);

	int n, i = 0;
	char buffer[bufsize];
	
	// read welcome message
	bzero(buffer, bufsize);
	n = read(sockfd, buffer, bufsize - 1);
	if (n < 0) {
		cerr << "failed to read from socket\n";
		exit(1);
	}
	cout << buffer;

	while (true) {
		n = write(sockfd, buffer, strlen(buffer));
		if (n < 0) {
			cerr << "real buf failure\n";
			exit(1);
		}

		// read %
		bzero(buffer, bufsize);
		n = read(sockfd, buffer, bufsize - 1);
		if (n < 0) {
			cerr << "failed to read from socket1\n";
			exit(1);
		}
		cout << buffer;

		// request
		bzero(buffer, bufsize);
		fgets(buffer, bufsize - 1, stdin);
		n = write(sockfd, buffer, strlen(buffer));
		if (n < 0) {
			cerr << "failed to write to socket\n";
			exit(1);
		}

		vector<string> cmd;
		splitCommand(buffer, cmd);

		// response form: <cid>,<resid>,<res>
		bzero(buffer, bufsize);
		n = read(sockfd, buffer, bufsize - 1);
		if (n < 0) {
			cerr << "failed to read from socket2\n";
			exit(1);
		}
		int stat = handler(buffer/*, cmd*/);
		if (stat == 1) {
			break;
		}
	}

	json test;
	test["abc"] = 123;
	test["def"] = "this is a test";

	cout << test.dump(4) << '\n';

	json tmp = json::parse(test.dump(4));
	cout << tmp["abc"] << '\n';
	cout << tmp.dump(2) << '\n';

	tmp.clear();
	cout << tmp << '\n';

	// cout << "create bucket\n";
	// Aws::SDKOptions options;
   	// Aws::InitAPI(options);
	// create_bucket("0616220-thisisanothertest");

	// Aws::S3::S3Client s3_client;
	// auto outcome = s3_client.ListBuckets();

	// if (outcome.IsSuccess())
	// {
	// 	std::cout << "Your Amazon S3 buckets:" << std::endl;

	// 	Aws::Vector<Aws::S3::Model::Bucket> bucket_list =
	// 		outcome.GetResult().GetBuckets();

	// 	for (auto const &bucket : bucket_list)
	// 	{
	// 		std::cout << "  * " << bucket.GetName() << std::endl;
	// 	}
	// }
	// else
	// {
	// 	std::cout << "ListBuckets error: "
	// 		<< outcome.GetError().GetExceptionName() << " - "
	// 		<< outcome.GetError().GetMessage() << std::endl;
	// }
	Aws::ShutdownAPI(options);
}