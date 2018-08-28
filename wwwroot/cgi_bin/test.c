////////////////////////////////////////////////////////
//此处实现的程序就是程序替换时执行的CGI程序
///////////////////////////////////////////////////////

#include "cgi_base.h"

int main(){
    //1.直接调用封装好的函数获取到参数
    char buf[1024 * 4] = {0}; 
    int ret = GetQueryString(buf);
    if(ret < 0){
        fprintf(stderr, "[CGI] GetQueryString failed\n");
        return 1;
    }
    //此时获取到的 buf 中的内容格式为 a=10&b=20
    //此时得到的是字符串，需要将参数进一步解析出来
    int a, b;
    sscanf(buf, "a=%d&b=%d", &a, &b);// sscanf 对于异常的错误处理不太方便，最好的方案就是进行字符串切分
    // sscanf 格式化的输入，可以从一个字符串中按照一定的格式化内容，来获取到我们需要的数据
    int sum = a + b;
    //printf 输出的结果就会被返回到客户端上，一定要严格遵守HTTP的响应格式
    //作为HTTP服务器，每次给客户返回的字符串必须符合HTTP协议的格式
    //由于父进程已经把 首行，header，空行都已经返回给客户端了
    //因此此时 CGI 程序只返回 body 部分即可，就是 html 格式的数据
    printf("<h1>sum=%d</h1>", sum);
    return 0;
}
