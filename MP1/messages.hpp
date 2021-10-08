#include <string.h>
#include <tuple>
#include <chrono>
using namespace std;

typedef struct multi{
    string ID;
    string address;
    string port;
    int sock;
}multicast;

typedef struct message_struct{
    int status;     // byte 0-3 // 0: proposal    1: reply    2: final
    char ID[50];    // byte 4-55
    int priority;   // byte 56-59
    char tiebreaker[30];    // byte 60-89
    char action[10];    // byte 90-99
    char originator[30];    // byte 100-129
    char destination[30];   // byte 134-159
    int amount; // byte 160-163
    int replies;
    unsigned long now;
}message;

// struct{
//     bool operator() (const message& a, const message& b){
//             return tie(a.priority, a.tiebreaker) < tie(b.priority, b.tiebreaker);
//         }
// }Compare;

struct{
    bool operator() (const message& a, const message& b){
            if(a.priority<b.priority) return true;
            if(b.priority<a.priority) return false;

            if(strcmp(a.tiebreaker, b.tiebreaker)<0){
                return true;
            }
            return false;
        }
}Compare;