#include <chrono>
#include <fstream>
#include <iostream>
#include <stack>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <queue>

using Maze = std::vector<std::vector<char>>;

struct Position {
    int row;
    int col;
};

// Variáveis globais
Maze maze;
int num_rows;
int num_cols;
std::mutex maze_mutex;
std::mutex cout_mutex;
std::mutex queue_mutex;
std::atomic<bool> exit_found(false);
std::condition_variable cv;
std::queue<std::stack<Position>> tasks;
std::atomic<int> active_threads(0);
const int MAX_THREADS = 4; // Limite de threads simultâneas

Position load_maze(const std::string &file_name) {
    std::ifstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Erro ao abrir o arquivo " << file_name << std::endl;
        exit(1);
    }

    Position start{-1, -1};
    file >> num_rows >> num_cols;
    maze.resize(num_rows, std::vector<char>(num_cols));
    for (int i = 0; i < num_rows; i++) {
        for (int j = 0; j < num_cols; j++) {
            file >> maze[i][j];
            if (maze[i][j] == 'e') {
                start.row = i;
                start.col = j;
            }
        }
    }
    return start;
}

void print_maze() {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "\033[H\033[J";

    for (int i = 0; i < num_rows; i++) {
        for (int j = 0; j < num_cols; j++) {
            if (maze[i][j] == '#') {
                std::cout << "\033[1;37m" << maze[i][j] << " \033[0m";
            } else if (maze[i][j] == 'o') {
                std::cout << "\033[1;32m" << maze[i][j] << " \033[0m";
            } else if (maze[i][j] == 'S' || maze[i][j] == 's') {
                std::cout << "\033[1;34m" << maze[i][j] << " \033[0m";
            } else {
                std::cout << maze[i][j] << " ";
            }
        }
        std::cout << '\n';
    }
    std::cout << std::endl;
}

bool is_valid_position(int row, int col) {
    if (row < 0 || row >= num_rows || col < 0 || col >= num_cols) {
        return false;
    }
    char cell;
    {
        std::lock_guard<std::mutex> lock(maze_mutex);
        cell = maze[row][col];
    }
    return cell == 'x' || cell == 's';
}

void worker() {
    while (!exit_found) {
        std::stack<Position> path;
        
        // Pegar nova tarefa da fila
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (tasks.empty()) {
                if (active_threads == 0) break; // Não há mais trabalho
                cv.wait(lock);
                continue;
            }
            
            path = tasks.front();
            tasks.pop();
            active_threads++;
        }

        if (path.empty()) {
            active_threads--;
            continue;
        }

        Position pos = path.top();
        path.pop();

        // Verificar se é a saída
        {
            std::lock_guard<std::mutex> lock(maze_mutex);
            if (maze[pos.row][pos.col] == 's') {
                exit_found = true;
                std::lock_guard<std::mutex> cout_lock(cout_mutex);
                std::cout << "Saída encontrada na posição (" << pos.row << ", " << pos.col << ")!\n";
                cv.notify_all();
                active_threads--;
                return;
            }
            maze[pos.row][pos.col] = 'o';
        }

        print_maze();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Verificar direções possíveis
        std::vector<Position> next_positions;
        if (is_valid_position(pos.row - 1, pos.col)) next_positions.push_back({pos.row - 1, pos.col});
        if (is_valid_position(pos.row, pos.col + 1)) next_positions.push_back({pos.row, pos.col + 1});
        if (is_valid_position(pos.row + 1, pos.col)) next_positions.push_back({pos.row + 1, pos.col});
        if (is_valid_position(pos.row, pos.col - 1)) next_positions.push_back({pos.row, pos.col - 1});

        // Adicionar novos caminhos à fila de tarefas
        if (!next_positions.empty()) {
            std::lock_guard<std::mutex> lock(queue_mutex);
            for (const auto& next_pos : next_positions) {
                std::stack<Position> new_path = path;
                new_path.push(next_pos);
                tasks.push(new_path);
            }
            cv.notify_one();
        }

        active_threads--;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Uso: " << argv[0] << " arquivo_maze.txt\n";
        return 1;
    }

    Position initial_pos = load_maze(argv[1]);
    if (initial_pos.row == -1 || initial_pos.col == -1) {
        std::cerr << "Posição inicial não encontrada no labirinto\n";
        return 1;
    }

    // Inicializar fila de tarefas
    std::stack<Position> initial_path;
    initial_path.push(initial_pos);
    tasks.push(initial_path);

    // Criar pool de threads
    std::vector<std::thread> threads;
    for (int i = 0; i < MAX_THREADS; ++i) {
        threads.emplace_back(worker);
    }

    // Aguardar todas as threads terminarem
    for (auto& t : threads) {
        t.join();
    }

    if (!exit_found) {
        std::cout << "Saída não encontrada!\n";
    }

    return 0;
}