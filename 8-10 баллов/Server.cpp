#include "common.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>

Programmer *programmers;
char message[256], observerMessage[256];
bool are_programmers_running = false;
int servSock, observerSock;
std::vector<int> observers;
std::mutex observerMutex;
std::queue<std::string> observerMessages;

void closeObserverSocket() {
  for (auto observer : observers) {
    close(observer);
  }
  close(observerSock);
}

void AddMessageToObservers(char *message) {
  std::lock_guard<std::mutex> lock(observerMutex);
  observerMessages.push((char *) message);
}

void DieWithError(const char *message) {
  error_message(message);
  perror("");
  close(servSock);
  closeObserverSocket();
  exit(1);
}

void sig_handler(int sig) {
  if (sig != SIGINT && sig != SIGQUIT && sig != SIGHUP) {
    error_message("Received an unknown signal");
    return;
  }
  if (are_programmers_running) {
    server_message("Sending a stop signal to the programmers. Suspending execution.");
    for (int i = 0; i < NUM_PROGRAMMERS; i++) {
      Task stop_task = Task{STOP, -1, -1};
      if (send(programmers[i].sock, &stop_task, sizeof(Task), 0) != sizeof(Task)) {
        sprintf(message, "Can not write stop task to programmer %d", i + 1);
        error_message(message);
      }
    }
  }
  close(servSock);
  closeObserverSocket();
  server_message("Bye!");
  exit(10);
}

void set_nonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

void handleObservers(int observerSock) {
  struct sockaddr_in observerAddr;
  socklen_t observerAddrLen = sizeof(observerAddr);

  while (true) {
    int newObserverSock = accept(observerSock, (struct sockaddr *) &observerAddr, &observerAddrLen);
    if (newObserverSock > 0) {
      set_nonblocking(newObserverSock);
      {
        std::lock_guard<std::mutex> lock(observerMutex);
        observers.push_back(newObserverSock);
      }
      sprintf(observerMessage, "Accepted connection from observer, %s:%d",
              inet_ntoa(observerAddr.sin_addr), ntohs(observerAddr.sin_port));
      server_message(observerMessage);
    }

    std::lock_guard<std::mutex> lock(observerMutex);

    while (!observerMessages.empty()) {
      const char* currentMessage = observerMessages.front().c_str();
      std::vector<std::vector<int>::iterator> toRemove;

      for (auto it = observers.begin(); it != observers.end(); ++it) {
        if (send(*it, currentMessage, strlen(currentMessage), MSG_NOSIGNAL) < 0) {
          toRemove.push_back(it);
          server_message("Observer disconnected");
        }
      }
      for (auto it : toRemove) {
        observers.erase(it);
      }
      observerMessages.pop();
    }
  }
}

int main(int argc, char *argv[]) {
  signal(SIGINT, sig_handler);
  signal(SIGQUIT, sig_handler);
  signal(SIGHUP, sig_handler);

  programmers = new Programmer[NUM_PROGRAMMERS];
  int clientSock;
  struct sockaddr_in servAddr, clientAddr, observerAddr;
  socklen_t clientAddrLen;
  Server server(programmers);

  if (argc != 2) {
    DieWithError("Incorrect number of arguments. Enter the server port");
    exit(1);
  }

  int servPort = atoi(argv[1]);
  OpenServerSocket(servPort, servSock, servAddr);
  if (listen(servSock, NUM_PROGRAMMERS) < 0) {
    DieWithError("listen() failed");
  }

  int observerPort = servPort + 1;
  OpenServerSocket(observerPort, observerSock, observerAddr);
  if (listen(observerSock, NUM_OBSERVERS) < 0) {
    DieWithError("listen() failed for observers");
  }
  set_nonblocking(observerSock);

  sprintf(message,
          "Server IP address = %s, port = %d. Waiting for %d programmers to connect...",
          inet_ntoa(servAddr.sin_addr),
          servPort,
          NUM_PROGRAMMERS);
  server_message(message);
  for (int i = 0; i < NUM_PROGRAMMERS; ++i) {
    AcceptConnection(servSock, clientSock, clientAddr, clientAddrLen, message);
    if (fcntl(clientSock, F_SETFL, O_NONBLOCK) < 0) {
      DieWithError("fcntl() failed");
    }
    programmers[i].id = i;
    programmers[i].sock = clientSock;
    programmers[i].echoServAddr = clientAddr;
  }
  server_message("All programmers connected. I am starting to manage the interaction...");

  // Creating a thread for observers
  std::thread observerThread(handleObservers, observerSock);
  // Allow all programmers to start working
  for (int i = 0; i < NUM_PROGRAMMERS; i++) {
    Task new_task = Task{Programming, -1, -1};
    if (send(programmers[i].sock, &new_task, sizeof(Task), 0) != sizeof(Task)) {
      close(servSock);
      DieWithError("Can not write task to programmer");
    }
    sprintf(message, "Assigned a new task: Programming to programmer №%d", i + 1);
    server_message(message);
    AddMessageToObservers(message);
  }
  int free_programmers = 0;

  while (1) {
    if (free_programmers != 0) {
      int id = server.find_free_programmer();
      if (!programmers[id].is_task_poped) {
        switch (programmers[id].current_task.task_type) {
          case TaskType::Programming:
            // push_back - обычная проверка имеет обычный приоритет
            server.task_list.push_back(Task{Checking, -1, id});
            programmers[id].is_program_checked = false;
            break;
          case TaskType::Checking:
            if (programmers[id].is_correct) {
              // push_front - исправление программы имеет главный приоритет
              server.task_list.push_front(Task{Fixing, programmers[id].current_task.id_linked, id});
            } else {
              programmers[programmers[id].current_task.id_linked].is_program_checked = true;
            }
            break;
          case TaskType::Fixing:
            // push_front - перепроверка имеет наивысший приоритет
            server.task_list.push_front(Task{Checking, programmers[id].current_task.id_linked, id});
            break;
        }
        programmers[id].is_task_poped = true;
      }

      Task new_task;
      bool is_new_task = false;
      for (auto it = server.task_list.begin(); it != server.task_list.end(); it++) {
        if (it->id_performer != id && it->id_performer != -1 || it->id_linked == id) {
          continue;
        } else {
          new_task = *it;
          server.task_list.erase(it);
          is_new_task = true;
          break;
        }
      }
      if (programmers[id].is_program_checked) {
        new_task = Task{Programming, -1, -1};
        is_new_task = true;
      }

      if (is_new_task) {
        programmers[id].current_task = new_task;
        if (send(programmers[id].sock, &new_task, sizeof(Task), 0) != sizeof(Task)) {
          error_message("Can not write task to programmer");
          perror("write");
          sig_handler(SIGINT);
        }
        sprintf(message, "Assigned a new task: %s to programmer №%d", TaskTypeNames[new_task.task_type], id + 1);
        server_message(message);
        AddMessageToObservers(message);
        programmers[id].is_task_poped = false;
        free_programmers--;
      }
    }
    for (int i = 0; i < NUM_PROGRAMMERS; i++) {
      int clientSock = programmers[i].sock;
      if (clientSock > 0) {
        bool is_correct;
        ssize_t bytesReceived = recv(clientSock, &is_correct, sizeof(bool), 0);
        if (bytesReceived == sizeof(bool)) {
          programmers[i].is_correct = is_correct;
          free_programmers++;
          sprintf(message, "Programmer №%d completed the task", i + 1);
          server_message(message);
          AddMessageToObservers(message);
        } else if (bytesReceived == 0) {
          sprintf(message, "Programmer №%d disconnected", i + 1);
          server_message(message);
          AddMessageToObservers(message);
          sig_handler(SIGINT);
        } else if (errno != EAGAIN) {
          close(servSock);
          DieWithError("recv() failed");
        }
      }
    }
  }
}
