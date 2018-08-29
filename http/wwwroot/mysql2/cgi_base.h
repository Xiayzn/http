#pragma once

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>

//注意一般 .h 文件只能放函数声明，不能放函数定义
//如果想把函数的定义放到函数里面，应该怎么做？
//

//分 GET 和 POST 读取计算的参数
//1.对于 GET 请求从 query_string 中读取
//2.对于 POST 请求 从 body 之中读取
//读取的结果就放在 buf 这个输出缓冲区中
static int GetQueryString(char buf[]){
    //1. 从环境变量中获取到方法是什么
    char* method = getenv("REQUEST_METHOD");
    if(method == NULL){ //method 获取到的可能是空指针，如果环境变量不存在，method 就是一个空指针
        //由于当前CGI程序对应的标准输出已经被重定向到管道上了，而这部分数据又会被返回给客户端
        //避免让程序内部的错误暴露给普通用户，通过 stderr , 来作为输入日志的手段
        fprintf(stderr, "[CGI] method is NULL\n");
        return -1;
        //如果直接打印 printf ，是往标准输出中打印，前面重定向了标准输入输出( child_read、child_write )，
        //我们在哪里能看到？客户端上,所以不适和用 printf 输出
    }
    //2. 进行判定，判定方法是 GET 还是 POST
    //如果是 GET 方法，就从环境变量里面读取 QUERY_STRING
    //如果是 POST 方法，就从环境变量中读取 CONTENT_LENGTH
    if(strcasecmp(method, "GET") == 0){
        char* query_string = getenv("QUERY_STRING");
        if(query_string == NULL){
            fprintf(stderr, "[CGI] query_string is NULL\n");
            return -1;
        }
        //拷贝完成之后， buf 里面的内容形如 a=10&b=20
        strcpy(buf, query_string);
    }else{
        char* content_length_str = getenv("CONTENT_LENGTH");
        if(content_length_str == NULL){
            fprintf(stderr, "[CGI] content_length is NULL\n");
            return -1;
        }
        int content_length = atoi(content_length_str);
        int i = 0;
        for(; i < content_length; ++i){
            //此处由于父进程把 body 已经写入管道，子进程又已经把 0 号文件描述符重定向到了管道
            //为什么是从 0 号文件描述符来读取数据放到 buf 里呢？此时从标准输入中读，也就读到了管道中的数据
            //
            read(0, &buf[i], 1);
            
        }
        //此处循环读完之后，buf 里面的内容就形如 a=10&b=20 (取出 body )
        buf[i] = '\0';
    }
    //上面代码就是把 GET、POST里的内容放到了缓冲区 buf 中，之后我们只有从 buf 之中读数据就行了 
    //分 GET POST 两种情况来处理数据，处理数据的方式还是按照CGI的方式来处理
    // CGI 约定了父进程和子进程之间如何来交互数据，交互的方式就是通过我们创建的管道
    // 对于 GET 请求来说，管道其实不用，GET 想要用来计算的依据都在 query_string 之中,只要读取数据就行了，
    // 而 query_string 是通过环境变量传递的过来的，
    // 因此对于 GET 请求来说，只要直接拿到环境变量里的内容，拷贝到 buf 里面也就完成了
    // 对于 POST 请求来说，body 部分才是我们用来计算的参数依据，body 部分通过管道来读取。
    // 首先我们得获得 content_length ,知道管道里面的数据有多长，再根据长度进行循环，
    // 这里循环结束之后也就将 body 中的数据放到 duf 缓冲区了
    return 0;
}
