#include <iostream>
#include "common.h"
#include "server.h"

pthread_mutex_t ticksLock;
pthread_mutexattr_t tickLockAttr;
using namespace std;

static ofstream logFile(LOGNAME, ofstream::app);

ostream & operator<<(ostream &out, PEERTICKTYPE &clientMap) {
    out << "----------->>> List" << endl;
    auto iter = clientMap.begin();
    for (; iter != clientMap.end(); ++iter) {
        out << "  " << iter->first.ip << ":" << iter->first.port;
        out << endl;
    }

    out << "<<< ---------------" << endl;
    return out;
}

ostream & operator<<(ostream &out, PEERPUNCHEDTYPE &hashMap) {
    out << "----------->>> List" << endl;
    auto iter = hashMap.begin();
    for (; iter != hashMap.end(); ++iter) {
    }
    out << "<<< ---------------" << endl;
    return out;
}

/* unordered_map remove duplicate items */
void addClient(PEERTICKTYPE &clientMap, const PeerInfo &peer) {
    pthread_mutex_lock(&ticksLock);
    clientMap[peer].tick = TICKS_INI;
    pthread_mutex_unlock(&ticksLock);

    return;
}

void delClient(PEERTICKTYPE &clientMap, const PeerInfo &peer) {
    pthread_mutex_lock(&ticksLock);
    auto iterFind = clientMap.find(peer);
    if (iterFind != clientMap.end()) {
        clientMap.erase(iterFind);
    } else {
        write2Log(logFile, "delete peer error: did not found.");
    }
    pthread_mutex_unlock(&ticksLock);

    return;
}

/* till now, did not use this function. */
void setReentrant(pthread_mutex_t &lock, pthread_mutexattr_t &attr) {
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lock, &attr);

    return;
}

void listInfo2Str(PEERTICKTYPE &clientMap, PEERPUNCHEDTYPE &punchMap,
                                           char *msg) {
    ostringstream oss;
    ostringstream pOss;
    string ip, port;

    oss << "\n-------------------------- *** Login Info\n"
        << std::left << std::setfill(' ')
        << "  "  << setw(21) << "PEERINFO-IP-PORT"
        << "  "  << setw(3)  << "TTL"
        << "   " << setw(8)  << "HOSTNAME\n";

    pthread_mutex_lock(&ticksLock);
    auto iter1 = clientMap.begin();
    for (; iter1 != clientMap.end(); ++iter1) {
        /* reformat port ip layout: 127.0.0.1 13000. */
        ip = iter1->first.ip;
        ip.push_back(' ');

        /* clear str of pOss, notice that function pOss.clear()
         * only reset the iostat. */
        pOss.str("");
        pOss.clear();
        pOss << iter1->first.port;
        port = pOss.str();
        ip += port;

        oss << "  "  << setw(21) << ip
            << "  "  << setw(3)  << iter1->second.tick
            << "   " << iter1->second.hostname << "\n";
    }
    oss << "*** --------------------------------------" << "\n";

    oss << "\n-------------------------- *** Punch Info\n";
    auto iter2 = punchMap.begin();
    for (; iter2 != punchMap.end(); ++iter2) {
        oss << "  " << iter2->first.ip << " " << iter2->first.port
            << "  " << " ===>> "
            << "  " << iter2->second.ip << " " << iter2->second.port
            << "\n";
    }
    pthread_mutex_unlock(&ticksLock);
    oss << "*** --------------------------------------" << endl;

    memset(msg, 0, IBUFSIZ);
    strcpy(msg, oss.str().c_str());
    return;
}

void *handleTicks(void *arg) {
    PEERTICKTYPE *hashMap = (PEERTICKTYPE *)arg;
    while (1) {
        pthread_mutex_lock(&ticksLock);
        /* Erasing an element of a map invalidates iterators pointing
         * to that element (after all that element has been deleted).
         * You shouldn't reuse that iterator, instead, advance the
         * iterator to the next element before the deletion takes place.
         */
        auto iter = hashMap->begin();
#if 0
        cout << "########## Enter --iter->second" << endl;
#endif
        while (iter != hashMap->end()) {
            --(iter->second).tick;
            if ((iter->second).tick < 0) {
                PeerInfo peer = iter->first;
                hashMap->erase(iter++);
#if 1
                /* check if punchMap still has this timeout info. */
                cout << "############################### Delete It" << endl;
                auto iterFind = punchMap.find(peer);
                if (iterFind != punchMap.end()) {
                    cout << "Found Timeout Peer: " << peer << endl;
                    cout << "#######################################" << endl;
                    punchMap.erase(iterFind);
                }
#endif
            } else {
                ++iter;
            }
        }

        pthread_mutex_unlock(&ticksLock);

        /* sleep 1 s before next lock action. sleep some time is must.*/
        sleep(1);
    }
    return NULL;
}

void onCalled(int sockFd, PEERTICKTYPE &clientMap,
                          PktInfo &packet, PeerInfo &peer) {
    char message[IBUFSIZ];
    memset(message, 0, IBUFSIZ);

    ssize_t recvSize = udpRecvPkt(sockFd, peer, packet);
    PKTTYPE type = packet.getHead().type;

    switch (type) {
        case PKTTYPE::MESSAGE:
            {
                /* check if peer has punched pair. */
                cout << "Message From " << peer << ". " << endl;
                pthread_mutex_lock(&ticksLock);
                auto iterFind = punchMap.find(peer);
                if (iterFind != punchMap.end()) {
                    /* TYPE SYN inform peer to fetch getHead().peer info
                     * from NET packet in addition with peer info. */
                    packet.getHead().type = PKTTYPE::SYN;
                    packet.getHead().peer = peer;
                    udpSendPkt(sockFd, punchMap[peer], packet);
                }
                pthread_mutex_unlock(&ticksLock);
                break;
            }
        case PKTTYPE::HEARTBEAT:
            {
                pthread_mutex_lock(&ticksLock);

                clientMap[peer].tick = TICKS_INI;
        /* fix bug: under some uncertein circumstance handleTicks()
         * will stop minus 'tick', so use upper code replacing below,
         * seems work good.
         */
#if 0
                auto iterFind = clientMap.find(peer);
                if (iterFind != clientMap.end()) {
                    (iterFind->second).tick = TICKS_INI;
                } else {
                    /* reentrant lock. */
                    addClient(clientMap, peer);
                }
#endif
                pthread_mutex_unlock(&ticksLock);
                cout << "Heart Beat Received From " << peer << endl;
                break;
            }
        case PKTTYPE::LOGIN:
            {
                addClient(clientMap, peer);
                cout << peer << " login." << endl;
                break;
            }
        case PKTTYPE::LOGOUT:
            {
                delClient(clientMap, peer);
#if 1
                /* check if punchMap still has this timeout info. */
                pthread_mutex_lock(&ticksLock);
                cout << "############################### Delete It" << endl;
                auto iterFind = punchMap.find(peer);
                if (iterFind != punchMap.end()) {
                    cout << "Found To Delete Peer: " << peer << endl;
                    cout << "#######################################" << endl;
                    punchMap.erase(iterFind);
                }
                pthread_mutex_unlock(&ticksLock);
#endif
                cout << peer << " logout." << endl;
                break;
            }
        case PKTTYPE::LIST:
            {
                listInfo2Str(clientMap, punchMap, message);
                makePacket(message, packet, PKTTYPE::MESSAGE);
                udpSendPkt(sockFd, peer, packet);
                break;
            }
        case PKTTYPE::PUNCH:
            {
                PeerInfo tPeer = packet.getHead().peer;
                cout << "From " << peer << " To " << tPeer << endl;

                /* check if both peer and tPeer has logined. */
                pthread_mutex_lock(&ticksLock);
                auto iterFind = clientMap.find(peer);
                auto pFind = clientMap.find(tPeer);
                if ((iterFind == clientMap.end())
                                  || (pFind == clientMap.end())) {
                    strcpy(message, "First, You Two Must All Be Logined.\
                                            \nJust Type 'list' to See Info.");
                    makePacket(message, packet, PKTTYPE::ERROR);
                    udpSendPkt(sockFd, peer, packet);
                    break;
                }

                packet.getHead().peer = peer;
                /* add to punchMap */
                punchMap[peer]  = tPeer;
                punchMap[tPeer] = peer;
                cout << punchMap << endl;
                pthread_mutex_unlock(&ticksLock);

                /* notice the punched peer. */
                udpSendPkt(sockFd, tPeer, packet);
                break;
            }
        case PKTTYPE::SYN:
            {
                cout << "From " << peer << endl;
                break;
            }
        case PKTTYPE::ACK:
            {
                break;
            }
        case PKTTYPE::WHOAMI:
            {
                makePacket(message, packet, PKTTYPE::WHOAMI);
                packet.getHead().peer = peer;
                udpSendPkt(sockFd, peer, packet);
                break;
            }
        case PKTTYPE::SETNAME:
            {
                /* get hostname sent by client and set it to TickInfo. */
                getSetHostName(clientMap, peer, packet.getPayload());
                break;
            }
        default:
            break;
    }
#if 1
    cout << packet << "\n" << endl;
#endif
       return;
}

void getSetHostName(PEERTICKTYPE &clientMap, PeerInfo &peer, char *payload) {
    char fWord[MAXHOSTLEN];
#if 0
    cout << "####### payload ****" << payload << endl;
#endif
    int cnt = 0;
    char *pTmp = payload;
    char *pSet = fWord;
    /* skip first command word till encountere a blank space. */
    while ((*pTmp != ' ') && (cnt < MAXHOSTLEN - 1)) {
        ++pTmp;
        ++cnt;
    }

    /* while (*pTmp++ == ' ');
     * will omit the first character of hostname. BUG
     * */
    while (*pTmp == ' ') {
        ++pTmp;
    }

    cnt = 0;
    while ((*pTmp != '\0') && (cnt < MAXHOSTLEN - 1)) {
        *pSet = *pTmp;
        ++pSet;
        ++pTmp;
        ++cnt;
    }
    fWord[cnt] = '\0';
    strncpy(clientMap[peer].hostname, fWord, MAXHOSTLEN - 1);

    return;
}
