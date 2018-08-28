#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<pthread.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/sendfile.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "http_server.h"

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in  sockaddr_in;

//一次从 socket 中读取一行数据
//把数据放到 buf 缓冲区之中
//如果读取失败，返回值就是 -1
//\n \r \r\n
int ReadLine(int sock, char buf[], ssize_t size){
    //1.从 socket 中一次读取一个字符
    char c = '\0';//一次读一个字符
    ssize_t i = 0;//下标作为循环判定的依据,i表示当前读了多少个字符，即字符的个数
    //结束条件： 如果读到 \n 就返回
    //a)读的长度太长，达到了缓冲区长度的上线
    //b)读到了'\n'(此处要兼容 \r \r\n 的情况，
    //如果遇到了 \r 或者 \r\n 想办法转换成 \n )
    while(i < size -1 && c != '\n'){
        ssize_t read_size = recv(sock, &c, 1, 0);
        if(read_size <= 0){
            //预期读到 \n 这样的换行符，结果还没读到呢
            //就先读到了 EOF，这种情况我们也暂时认为是失败的
            return -1;
        }
        if(c == '\r'){
            //如果遇到了 \r ，但是还需要确定下一个字符是不是 \n
            //MSG_PEEK 选项从内核的缓冲区中读取数据
            //但是读到的数据不会从缓冲区中删除掉
            recv(sock, &c, 1, MSG_PEEK);
            if(c == '\n'){
                //此时整个的分隔符就是 \r\n ,再读一次把\n从缓冲区删掉
                recv(sock, &c, 1, 0);
            }else{
                //强制赋值成 \n ，此时当前的分隔符确定就是 \r ,此时就把分隔符转换成 \n
                c = '\n';
            }
        }
        //只要上面 c 读到的是 \r，那么 if 结束之后， c 都变成了 \n 
        //这种方式就是把前面的 \r 和 \r\n 两种情况都统一成了 \n
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

int Split(char input[],const char* split_char, char* output[], 
        int output_size){
    //使用 strtok — 字符串切割函数 实现的 Split ，但使用的时候需要循环调用
    int i = 0;
    char* tmp = NULL;
    char* pch;
    //使用线程安全的 strtok_r 代替 strtok 
    //这一点是以后非常容易出错的一点
    pch = strtok_r(input, split_char, &tmp);
    while(pch != NULL){
        if(i >= output_size){
            return i;
        }//超过数组下标导致放不下
        output[i++] = pch;
        pch = strtok_r(NULL, split_char, &tmp);
    }
    return i;//strtok内部保存了上次切分的结果，问题来了：这个结果用什么样类型数据保存？用静态变量保存，函数结束后变量仍然存在，函数内部使用静态变量意味着什么？意味着不可重入，这样一个函数也是线程不安全的函数，多个线程调用可能会出现逻辑上的问题
}

int ParseFirstLine(char first_line[], char ** p_url, char** p_method){
    //把首行按照字符串进行切分
    char* tok[10];
    //把 first_line 按照空格进行字符串切分
    //切分得到的每一个部分，就放到 tok 数组之中
    //返回值，就是 tok 数组中包含几个元素
    //最后一个参数 10 表示 tok 数组最多能放几个元素
    int tok_size = Split(first_line, " ", tok, 10);
    if(tok_size != 3){//如果切分结果不等于三，就认为当前首行有问题
        printf("Split failed! tok_size=%d\n", tok_size);//打印错误日志
        return -1;//表示切分失败
    }
    *p_method = tok[0];
    *p_url = tok[1];
    return 0;
}

int ParseQueryString(char* url, char** p_url_path, 
        char** p_query_string){
    *p_url_path = url;
    char* p = url;
    //printf("[ParseQueryString] %s %s %s\n", url, *p_url_path, *p_query_string);
    for(; *p != '\0'; ++p){
        if(*p == '?'){
            *p = '\0';
            *p_query_string = p + 1;
            return 0;
        }
    }
    //循环结束都没找到 ？ ，说明这个请求不带 query_string
    *p_query_string = NULL;
    //printf("[ParseQueryString] %s %s %s\n", url, *p_url_path, *p_query_string);
    return 0;
}

int ParseHeader(int sock, int* content_length){
    char buf[SIZE] = {0};//先定义缓冲区大小，用来存放每一行，在头文件中定义过的
    while(1){
        //1.循环从 socket 中读取一行
        ssize_t read_size = ReadLine(sock, buf, sizeof(buf));//为什么这里buf不 -1 了？1.初始化为全0;2.ReandLine函数中循环条件是 size -1 所以外部不用再考虑
        //处理读失败的情况
        if(read_size <= 0){
            return -1;//失败结束函数
        }
        //处理读完的情况，读到空行，循环结束
        if(strcmp(buf, "\n") == 0){
            return 0;
        }
        //2.判断当前行是不是Content_Length
        //  如果是Content_Length就直接把value读取出来
        //  如果不是就直接丢弃
        const char* content_length_str = "Content-Length: ";
        if(content_length != NULL
                && strncmp(buf, content_length_str, 
                    strlen(content_length_str)) == 0){//怎么判断字符串以哪个字符开头？用strncmp判断指定情况下前n个字符是否相同
            *content_length = atoi(buf + strlen(content_length_str));//从socket读到的字符串是Content-Length: 27,怎么取出27  buf+"Content-Length: "的长度
        }
    }
    return 0;
}

void Handler404(int sock){
    //一旦触发404，需要构造一个完整的HTTP响应
    //状态码就是404
    //返回的 body 部分应该也是404相关的页面
    const char* first_line = "HTTP/1.1 404 NotFound\n";//第一行—首行
    const char* type_line = "Content-Type: text/html;charset=utf-8\n";
    const char* blank_line = "\n";//空行
    const char* html = "<head><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">"
                       "</head><h1>您的页面被喵星人吃掉了！！！</h1>";
    send(sock, first_line, strlen(first_line), 0);
    send(sock, type_line, strlen(type_line), 0);
    send(sock, blank_line, strlen(blank_line), 0);
    send(sock, html, strlen(html), 0);
    return;
}

void PrintRequest(Request* req){
    printf("method: %s\n", req->method);
    printf("url_path: %s\n", req->url_path);
    printf("query_string: %s\n", req->query_string);
    printf("content_length: %d\n", req->content_length);
    return;
}

int IsDir(const char* file_path){
    struct stat st;
    int ret = stat(file_path, &st);//通过这个函数获取目录信息
    if(ret < 0){
        return 0;
    }
    if(S_ISDIR(st.st_mode)){//通过宏的这个选项就可以判定它是不是目录了
        return 1;
    }
    return 0;
}

void HandlerFilePath(const char* url_path, char file_path[]){
    // 用于生成路径的函数,把 url_path ,转换成真实路径保存在 file_path 中
    // a) 给 url_path 加上前缀(HTTP服务器的根目录)
    // url_path => /index.html
    // file_path => ./wwwroot/index.html
    sprintf(file_path, "./wwwroot%s", url_path);
    // b) 例如 url_path => /，此时 url_path 其实是一个根目录
    //    如果是目录的话，就给这个目录之中追加上一个 index.html
    //    这种情况相当于路径为 url_path/ 或者 /image/
    if(file_path[strlen(file_path) - 1] == '/'){//如何取最后一个字符？ strlen(file_path)-1,就是最后一个字符的下标
        strcat(file_path, "index.html");//用哪个函数进行追加？strcat()
    }
    // c) 第三种情况 url_path => /image
    if(IsDir(file_path)){
        strcat(file_path, "/index.html");
    }
    return;
}

ssize_t GetFileSize(const char* file_path){
    struct stat st;//获取文件源信息,文件类型也是根据它获取的
    int ret = stat(file_path, &st);
    if(ret < 0){
        //打开文件失败，很可能文件不存在
        //此时直接返回文件长度为0
        return 0;
    }
    return st.st_size;//该值就是文件大小
}

int WriteStaticFile(int sock, const char* file_path){
    //1.打开文件
    //什么情况下文件会打开失败？1.文件描述符不够用（文件描述符也是一个稀有资源）2.文件不存在
    int fd = open(file_path, O_RDONLY);//直接用系统调用,file_path 是通过 url_path 传进来的，这是用户输入过来的信息
    if(fd < 0){//文件打开失败,返回错误的状态码
        perror("open");
        return 404;
    }
    //2.把构造出来的HTTP响应写入到socket之中
    //  a) 写入首行
    const char* first_line = "HTTP/1.1 200 OK\n";
    send(sock, first_line, strlen(first_line), 0);
    //  b) 写入header
    //  文件按图片还是文本解析需要做区分
    //  按文本
    //const char* type_line = "Content-Type: text/html;charset=utf-8\n";
    //按图片
    //const char* type_line = "Content-Type: image/jpg;charset=utf-8\n";
    //如果一股脑的按文本或者图片解析都不合理
    //send(sock, type_line, strlen(type_line), 0);
    //注意不添加type_line这个类型，就是让浏览器自己识别是什么类型

    //  c) 写入空行
    const char* blank_line = "\n";
    send(sock, blank_line, strlen(blank_line), 0);
    //  d) 写入body（文件内容）必须严格按照HTTP的响应的报文格式构造
    //如何保证把文件中的所有字节都读出来？read读不确定，一个字节一个字节的读，首先要获取文件的内容有多长,这个方法可以，但是不够高效
    /* 
    ssize_t file_size = GetFileSize(first_path);
    ssize_t i = 0;
    for(; i< file_size; ++i){
        char c;
        read(fd, &c, 1, 0);
        send(sock, &c, 1, 0);
    }
    */
    //sock：用来写的sock  fd：用来读的sock  NULL：表示偏移量从文件开始来计算/来读取    最后一个参数表示文件大小  
    sendfile(sock, fd, NULL, GetFileSize(file_path));
    //3.关闭文件
    close(fd);
    return 200;
}

int HandlerStaticFile(int sock, Request* req){//为了让函数编译通过定义一下函数
    //1.根据 url_path 获取到文件在服务器上的真实路径
    char file_path[SIZE] = {0};//用 file_path 来保存文件的真实路径
    HandlerFilePath(req->url_path, file_path);
    //2.读取文件，把文件的内容直接写到 socket 之中
    int err_code = WriteStaticFile(sock, file_path);//通过这个函数来完成写 socket 的过程

    return err_code;
}

int HandlerCGIFather(int new_sock, int father_read, int father_write, int child_pid, Request* req){
    //1. 如果是 POST 请求，就把 body写入到管道之中
    if(strcasecmp(req->method, "POST") == 0){
        int i = 0;//开始写 body  问题：body 的长度一共有多长？ content_length
        char c = '\0';
        for(; i < req->content_length; ++i){
            read(new_sock, &c, 1);//从 new_sock 中读，读到 c 中，长度为 1，每次读一个字符
            //为什么从sock中读数据读到的就是 body 部分？这是为什么？
            //因为前面已经对 new_sock 读过很多次了，HandlerCGIFather() 该函数是在 HandlerCGI() 函数中调用的，
            // HandlerCGI() 函数是在前面对于头部进行解析之后才调用的，我们可以看到，在首行读取过 new_sock ,
            // 在处理 header 的时候我们又循环读过 new_sock，所以最终我们把 new_sock中的 首行 、 header 和 空行 都读完了
            // 然后我们再去读必然就是 body
            write(father_write, &c, 1);//读到一个字符就往管道里写一个，写到 father_write 之中
        }
    }
    //2. 构造HTTP响应
    const char* first_line = "HTTP/1.1 200 OK\n";
    send(new_sock, first_line, strlen(first_line), 0);
    const char* type_line = "Content-Type: text/html;charset=utf-8\n";
    send(new_sock, type_line, strlen(type_line), 0);
    const char* blank_line = "\n";
    send(new_sock, blank_line, strlen(blank_line), 0);

    //3. 写入HTML的部分，也就是CGI程序写回给我们的服务器，通过管道写回给服务器的代码
    //需要循环的从管道中读取数据，并写入到 socket
    //我们再读取之前根本不知道子进程返回的长度有多长
    char c = '\0';
    while(read(father_read, &c, 1) >0){
        send(new_sock, &c, 1, 0);
        //如果read <= 0 我们就跳出循环，read 什么时候会小于0(<0)?失败 什么时候等于0(=0)?读到文件结束标志EOF
        //我们现在操作的是管道，father 反复的尝试从 fd1[0] 中读，读到一定程度，读到了返回长度为0，就认为读完了
        //但是这是有一个前提的，什么样的前提？更具体一点，什么情况下才是EOF呢？
        //管道所涉及到的特性：
        //(1)如果管道所有的写端全部关闭，再尝试读，返回什么？ read 返回 0 
        //（这个就是 read 返回 0 的前提, 写端都被哪些进程打开的? 
        // a).子进程 b).因为先创建的管道，再创建的子进程，所以在fork的一瞬间，父子进程各自有一份文件描述符表，
        //并且这两份文件描述符表是一样的，所以换句话来说，这个管道被父子进程都打开了一半，
        //所以这是我么前面父进程先关闭子进程的文件描述符、子进程关闭父进程文件描述符的原因，
        //这样就只有子进程持有这两个文件描述符了，当子进程结束退出，对应的写端也就全部关闭了，
        //从而父进程再一读管道，才能够对应的返回0 ）
        //(2)如果管道所有的读端全部关闭，再尝试写，会发生什么？ 会产生 SIGPIPE信号，导致进程异常终止
    }
    //4.只要我们创建子进程，就要明确子进程谁来回收
    //  回收子进程的资源 （a.自动回收 b.手动指定）
    waitpid(child_pid, NULL, 0);//对于父进程来书，fork的返回值就是子进程的id
    
    return 200;
}

int HandlerCGIChild(int child_read, int child_write, Request* req){
    //1.设置环境变量 — 把必要的参数通过环境变量，给我们程序替换之后的进程
    //  一旦进行程序替换之后，像 Request 这样的变量就看不到了，所以通过设置环境变量传递
    char method_env[SIZE] = {0};//定义一个字符数组，将其构造成环境变量这样的一个键值对
    sprintf(method_env, "REQUEST_METHOD=%s", req->method);//进行字符串的格式化的拼接，拼接成完整的键值对字符串
    putenv(method_env);//进行环境变量的设置
    //还需要设置 QUERY_STRING 或者 Content_length
    if(strcasecmp(req->method, "GET") == 0){
        char query_string_env[SIZE] = {0};
        sprintf(query_string_env, "QUERY_STRING=%s", req->query_string);//这样写是根据CGI标准来的
        putenv(query_string_env);//把环境变量放进去
    }else{
        char content_length_env[SIZE] = {0};
        sprintf(content_length_env, "CONTENT_LENGTH=%d", req->content_length);
        putenv(content_length_env);
    }
    //2.把标准输入输出重定向到管道中
    dup2(child_read, 0);
    dup2(child_write, 1);
    //3.对子进程进行程序替换
    //  根据 url_path: /cgi-bin/test 首先得获取到程序所对应的文件在哪里
    //  file_path: ./wwwroot/cgi-bin/test
    char file_path[SIZE] = {0};
    HandlerFilePath(req->url_path, file_path);
    //进行程序替换，exec有6个函数
    //l lp le
    //v vp ve
    execl(file_path, file_path, NULL);
    exit(1);
    //程序替换有没有可能会失败？有可能
    //一旦失败了怎么办？
    //一旦子进程失败，就要让子进程终止，如果不终止，因为父子进程是同一份代码，
    //子进程、父进程都会在listen端口等待数据，然后出现父子进程都在等待端口数据返回的情况
    return 200; 
}

int HandlerCGI(int new_sock, Request * req){
    int err_code = 200;
    //1. 创建一对匿名管道
    int fd1[2], fd2[2];//创建的两组匿名管道，每组匿名管道都有一对文件描述符。注意：创建出来的匿名管道的名称命名规则不太好，非常忌讳给变量名后面加数字，这样可读性和可描述性都非常差，后面我们再取成新的名字，让其可描述性更好一些
    int ret = pipe(fd1);//创建匿名管道会失败吗？会的，当文件描述符不够的时候，创建匿名管道就会失败
    if(ret < 0){
        return 404;
    }
    ret = pipe(fd2);
    if(ret < 0){
        //一旦执行到这里，说明上面的fd1文件描述符创建成功，而fd2创建失败，在退出之前需要关闭fd1的文件描述符
        close(fd1[0]);
        close(fd1[1]);
        return 404;
    }
    //创建2对管道是为了既能够读，也能够写，让父子进程之间进行全双工的双向通信，
    //因此必须分配好，也就是划分开文件描述符的功能，哪个文件描述符是给父进程读，哪个文件描述符是给父进程来写，哪个文件描述符是给子进程读，哪个文件描述符是给子进程写，所以需要定义有描述性的文件描述符
    //fd1 fd2 这样的变量名描述性太差，后面直接使用的话是非常容易弄混的，
    //所以直接在此处定义更加明确的变量名来描述该文件描述符的用途
    int father_read = fd1[0];
    int child_write = fd1[1];//父进程从管道读，那么子进程就往管道写
    int father_write = fd2[1];
    int child_read = fd2[0];

    //2. 创建子进程
    ret = fork();
    //3. 父子进程各自执行不同的逻辑
    if(ret > 0){
        //father
        //此处父进程优先关闭这两个管道的文件描述符，是为了后续父进程从子进程这里读数据的时候，能够读到EOF
        //对于管道来说，所有的写端关闭，继续读，才能读到EOF
        //而此处的写端，一方面是父进程需要关闭，另一方面子进程也需要关闭
        //所以此处父进程先关闭不必要的写端之后，后续子进程用完了，直接关闭，父进程也就读到了EOF
        //否则就可能出现卡死的情况，不能够正常运行
        close(child_read);
        close(child_write);
        //先考虑传递的参数，我们需要把父进程这样一个数据写回到sock之中，另外一方面我们需要father_read, father_write
        err_code = HandlerCGIFather(new_sock, father_read, father_write, ret, req);// ret 就是fork的返回值
    }else if(ret == 0){
        //child
        close(father_read);
        close(father_write);
        //子进程流程的参数，要不要读写 socket ？不用，都是父进程完成的，子进程操作的是管道
        err_code = HandlerCGIChild(child_read, child_write, req);
    }else{
        perror("fork");
        err_code = 404;
        goto END;//返回之前需要关闭上面创建的4个描述符
    }
    //4.收尾工作和错误处理
END:
    close(fd1[0]);
    close(fd1[1]);
    close(fd2[0]);
    close(fd2[1]);
    return err_code;
}

void HandlerRequest(int new_sock){
    int err_code = 200;
    //1.读取并解析请求（反序列化——把字符串解析成request对象）
    Request req;
    memset(&req, 0, sizeof(req));
    //a)从 socket 中读取出首行，从 socket 文件描述符中先读出首行再说
    if(ReadLine(new_sock, req.first_line, sizeof(req.first_line)) < 0){
        //TODO 失败处理
        err_code = 404;
        goto END;
    }
    //b)解析首行，从首行中解析出 url 和 method（使用空格进行切分）
    if(ParseFirstLine(req.first_line, &req.url, &req.method)){
        //TODO 失败处理
        err_code = 404;
        goto END;
    }
    //c)解析 url，从url之中解析出url_path，query_string
    if(ParseQueryString(req.url, &req.url_path, 
                &req.query_string)){
        //TODO 失败处理
        err_code = 404;
        goto END;
    }
    //d)处理 header ，这里我们丢弃了大部分 header，只读取Content_length
    if(ParseHeader(new_sock, &req.content_length)){
        //TODO 失败处理
        err_code = 404;
        goto END;
    }
    PrintRequest(&req);
    //2.静态/动态方式生成页面，把生成结果写回到客户端上
    //假如浏览器发送的请求方法叫做 "get" / "Get" strcmp()就不再适用了
    //不区分大小写的比较 strcasecmp()
    if(strcasecmp(req.method, "GET") == 0 
            && req.query_string == NULL){
        //a)如果请求是 GET 请求，并且没有 query_string (没参数)，
        //  那么就返回静态页面
        err_code = HandlerStaticFile(new_sock, &req);
    }else if(strcasecmp(req.method, "GET") == 0 
            && req.query_string != NULL){
        //b)如果请求是 GET 请求，并且有 query_string ，
        //  那么就返回动态页面
        err_code = HandlerCGI(new_sock, &req);
    }else if(strcasecmp(req.method, "POST") == 0){
        //c)如果请求是 POST请求，本身就没有query_string ，但是 body 一定是带参数的，
        //  参数是通过 body 部分来传给服务器的，那么也是返回动态页面
        err_code = HandlerCGI(new_sock, &req);
    }else{
        //TODO 错误处理
        err_code = 404;
        goto END;
    }
    //错误处理:直接返回一个404的HTTP响应
END:
    if(err_code != 200){
        Handler404(new_sock);
    }
    close(new_sock);//意味着断开连接，服务器先断开就会出现TIME_WAIT状态，如果单位时间内接受的请求比较多，就会出现大量的TIME_WAIT,服务器就不能处理新的请求了
    return;
}

void* ThreadEntry(void* arg){
    int new_sock = (int)arg;
    //此处使用 HandlerRequest  函数来完成具体的处理请求过程，
    //这个过程单独提取出来是为了解耦合，
    //一旦需要把服务器改成多进程或者IO多路杜勇的形式，
    //整体代码的改动都是比较小的。
    HandlerRequest(new_sock);
    return NULL;
}

//服务器启动
void HttpServerStart(const char* ip, short port){
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);//1.第一步
    if(listen_sock < 0){
        perror("socket");
        return;
    }

    //加上这个选项就能够重用已有的 TIME_WAIT 连接
    //出现大量的 close_wait 状态意味着什么？
    //意味着代码出现了bug，4次挥手只执行了两次，服务器并没有正确的关闭 new_sock ,导致服务器出现了大量的 close_wait,这就是文件描述符泄露，需要我们对代码进行修复
    int opt = 1;//表示开启我们的选项,这是一个通用的类型
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, 
            &opt, sizeof(opt));//第一个参数表示对哪个参数设置值，后面两个参数表示要给哪个选项设置值，后两个参数决定设置的具体选项
    
    sockaddr_in addr;//2.第二步
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);
    int ret = bind(listen_sock, (sockaddr*)&addr, sizeof(addr));
    if(ret < 0){
        perror("bind");
        return;
    }
    ret = listen(listen_sock, 5);//3.第三步
    if(ret < 0){
        perror("listen");
        return;
    }
    printf("ServerInit OK.\n");
    while(1){//4.进入事件循环
        sockaddr_in peer;
        socklen_t len = sizeof(peer);//为了调用accept做准备
        int new_sock = accept(listen_sock, (sockaddr*)&addr, &len);
        if(new_sock < 0){
            perror("accept");
            continue;
        }
        //使用多线程的方式来实现TCP服务器
        pthread_t tid;
        pthread_create(&tid, NULL, ThreadEntry, (void*)new_sock);
        pthread_detach(tid);//创建线程之后必须回收，想要系统自动回收的话使用函数 pthread_detach();
    }
}

//启动命令： ./http_server [ip] [port]
int main(int argc, char* argv[]){
    if(argc != 3){
        printf("Usage ./http_server [ip] [port]\n");
        return 1;
    }
    HttpServerStart(argv[1], atoi(argv[2]));//调用服务器初始化函数
    return 0;
}
