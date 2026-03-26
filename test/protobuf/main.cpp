#include "./test.pb.h"
#include <iostream>
#include <string>
int func01 ()
{
    // 封装了login请求对象的数据
    fixbug::LoginRequest req;
    req.set_name("zhangsan");
    req.set_pwd("123456");

    // 对象数据序列化 char *
    std::string send_str;
    if (req.SerializeToString(&send_str))   // 序列化，将序列化成功的字符串放到了send_str的地址中
    { 
        std::cout << send_str.c_str() << std::endl;
    }

    // 从send_str反序列化一个login请求对象
    fixbug::LoginRequest request;
    if ( request.ParseFromString(send_str) )    // 反序列化，将反序列化成功的对象放到request
    {
        std::cout << request.name() << std::endl;
        std::cout << request.pwd() << std::endl;
    }

    return 0;
}

int func02 ()
{
    fixbug::LoginResponse resp;
    fixbug::ResultCode *rc = resp.mutable_result();
    
    rc->set_errcode(1);
    rc->set_msg("登录处理失败了");
    //  resp.set_success(false);    已经知道登录失败也没必要设置success

    return 0;
}

int func03 ()
{
    fixbug::GetFriendListResponse resp;
    fixbug::ResultCode *rc = resp.mutable_result();
    
    rc->set_errcode(0);
    rc->set_msg("获取好友列表成功");

    fixbug::User *user1 = resp.add_friend_list();
    user1->set_name("zhangsan");
    user1->set_sex(fixbug::User::MAN);
    user1->set_age(20);

    std::cout << resp.friend_list_size() << std::endl;  // 获取好友的个数

    fixbug::User *user2 = resp.add_friend_list();
    user2->set_name("li si");
    user2->set_sex(fixbug::User::MAN);
    user2->set_age(22);

    std::cout << resp.friend_list_size() << std::endl;  // 获取好友的个数

    for (int i = 0 ; i < resp.friend_list_size() ; i++)
    {
        std::cout << resp.friend_list(i).name() << " " << resp.friend_list(i).age() << " " << resp.friend_list(i).sex() << std::endl;
    }
    return 0;
}

int main ()
{
    func03();

    return 0;
}