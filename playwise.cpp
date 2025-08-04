#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <memory>
#include <random>
#include <functional>
#include <sstream>

using namespace std;

// Forward declarations
class PlayWiseEngine;

// ==============================================================================
// 1. Core Data Object: Song
// ==============================================================================
struct Song {
    string id;
    string title;
    string artist;
    int duration; // in seconds
    int rating;   // 1-5
    long dateAdded;

    Song(string t, string a, int d) : title(move(t)), artist(move(a)), duration(d), rating(0) {
        static long long counter = 0;
        id = "S" + to_string(++counter);
        dateAdded = chrono::system_clock::to_time_t(chrono::system_clock::now());
    }

    void print() const {
        int mins = duration / 60;
        int secs = duration % 60;
        cout << "ID: " << id << " | '" << title << "' by " << artist << " ("
             << setw(2) << setfill('0') << mins << ":"
             << setw(2) << setfill('0') << secs << ") "
             << "[Rating: " << (rating == 0 ? "N/A" : to_string(rating)) << "]";
    }
};

// ==============================================================================
// 2. Command Pattern for Undo/Redo
// ==============================================================================
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void undo() = 0;
    virtual string getDescription() = 0;
};

class AddCommand : public ICommand {
private:
    PlayWiseEngine& engine;
    string songId;
public:
    AddCommand(PlayWiseEngine& eng, string id) : engine(eng), songId(move(id)) {}
    void undo() override; // Implemented after PlayWiseEngine
    string getDescription() override { return "Undo Add Song"; }
};

class DeleteCommand : public ICommand {
private:
    PlayWiseEngine& engine;
    shared_ptr<Song> deletedSong;
    int originalIndex;
public:
    DeleteCommand(PlayWiseEngine& eng, shared_ptr<Song> song, int index) 
        : engine(eng), deletedSong(move(song)), originalIndex(index) {}
    void undo() override;
    string getDescription() override { return "Undo Delete Song"; }
};

class MoveCommand : public ICommand {
private:
    PlayWiseEngine& engine;
    string songId;
    int fromIndex;
    int toIndex;
public:
    MoveCommand(PlayWiseEngine& eng, string id, int from, int to) 
        : engine(eng), songId(move(id)), fromIndex(from), toIndex(to) {}
    void undo() override;
    string getDescription() override { return "Undo Move Song"; }
};

class CommandManager {
private:
    stack<unique_ptr<ICommand>> undoStack;
public:
    void execute(unique_ptr<ICommand> command) {
        undoStack.push(move(command));
    }

    void undo(int steps = 1) {
        for (int i = 0; i < steps; ++i) {
            if (undoStack.empty()) {
                cout << "No more actions to undo." << endl;
                return;
            }
            auto& cmd = undoStack.top();
            cout << cmd->getDescription() << "..." << endl;
            cmd->undo();
            undoStack.pop();
        }
    }
};

// ==============================================================================
// 3. Main Engine Class
// ==============================================================================
class PlayWiseEngine {
private:
    // --- Core Data Structures ---
    unordered_map<string, shared_ptr<Song>> allSongs;
    map<int, vector<weak_ptr<Song>>> songRatings;
    struct Node {
        weak_ptr<Song> song;
        unique_ptr<Node> next;
        Node* prev;
    };
    unique_ptr<Node> head;
    Node* tail;
    int playlistCount;
    stack<weak_ptr<Song>> playbackHistory;
    CommandManager commandManager;

public:
    PlayWiseEngine() : playlistCount(0), head(nullptr), tail(nullptr) {}

    // --- Public method to access CommandManager ---
    void undoLastEdit(int steps = 1) {
        commandManager.undo(steps);
    }

    // --- Playlist Engine (Linked List) ---
    void addSongToPlaylist(const string& title, const string& artist, int duration) {
        auto newSong = make_shared<Song>(title, artist, duration);
        allSongs[newSong->id] = newSong;

        auto newNode = make_unique<Node>();
        newNode->song = newSong;
        newNode->prev = tail;

        if (!head) {
            head = move(newNode);
            tail = head.get();
        } else {
            tail->next = move(newNode);
            tail = tail->next.get();
        }
        playlistCount++;
        cout << "Added: "; newSong->print(); cout << endl;
        
        commandManager.execute(make_unique<AddCommand>(*this, newSong->id));
    }
    
    void deleteSongFromPlaylist(int index) {
        if (index < 1 || index > playlistCount) {
            cerr << "Error: Invalid index." << endl;
            return;
        }
        
        Node* nodeToDelete = getNodeByIndex(index);
        if(!nodeToDelete) return;

        auto song = nodeToDelete->song.lock();
        cout << "Deleted: "; song->print(); cout << endl;

        commandManager.execute(make_unique<DeleteCommand>(*this, song, index));
        removeSongInternal(song->id);
    }

    void moveSong(int fromIndex, int toIndex) {
        if (fromIndex == toIndex || fromIndex < 1 || fromIndex > playlistCount || toIndex < 1 || toIndex > playlistCount + 1) {
             cerr << "Error: Invalid index for move operation." << endl;
             return;
        }

        auto song = removeSongAtIndex(fromIndex);
        insertSongAt(song, toIndex);
        
        cout << "Moved "; song->print(); cout << " from " << fromIndex << " to " << toIndex << endl;
        commandManager.execute(make_unique<MoveCommand>(*this, song->id, fromIndex, toIndex));
    }

    void reversePlaylist() {
        if (!head || !head->next) return;
        auto songs = getPlaylistAsVector();
        std::reverse(songs.begin(), songs.end());
        rebuildPlaylistFromVector(songs);
        cout << "Playlist reversed." << endl;
    }

    void printPlaylist() const {
        cout << "\n--- Playlist (" << playlistCount << " songs) ---" << endl;
        if (!head) { cout << "  (empty)" << endl; return; }
        Node* current = head.get();
        int index = 1;
        while (current) {
            cout << "  " << index++ << ". ";
            if (auto s = current->song.lock()) { s->print(); }
            cout << endl;
            current = current->next.get();
        }
        cout << "---------------------------\n" << endl;
    }

    // --- Playback History (Stack) ---
    void playSong(int index) {
        Node* node = getNodeByIndex(index);
        if (node) {
            if(auto song = node->song.lock()) {
               playbackHistory.push(song);
               cout << "Playing: "; song->print(); cout << endl;
            }
        } else {
            cerr << "Error: Invalid index." << endl;
        }
    }
    
    void playSong(const string& query) {
        shared_ptr<Song> songToPlay = nullptr;
        if (allSongs.count(query)) {
            songToPlay = allSongs[query];
        } else {
            string lowerQuery = query;
            transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
            vector<shared_ptr<Song>> foundSongs;
            for (const auto& pair : allSongs) {
                string lowerTitle = pair.second->title;
                transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
                if (lowerTitle.find(lowerQuery) != string::npos) {
                    foundSongs.push_back(pair.second);
                }
            }
            if (foundSongs.size() == 1) {
                songToPlay = foundSongs[0];
            } else if (foundSongs.empty()) {
                cout << "No song found matching '" << query << "'." << endl;
            } else {
                cout << "Multiple songs found. Please be more specific or use an index." << endl;
            }
        }

        if (songToPlay) {
            playbackHistory.push(songToPlay);
            cout << "Playing: "; songToPlay->print(); cout << endl;
        }
    }
    
    void undoLastPlay() {
        undoLastPlays(1);
    }

    void undoLastPlays(int steps) {
        if (steps <= 0) {
            cout << "Invalid number of steps." << endl;
            return;
        }
        
        int undoneCount = 0;
        vector<shared_ptr<Song>> songsToRequeue;
        for (int i = 0; i < steps && !playbackHistory.empty(); ++i) {
            if (auto lastPlayed = playbackHistory.top().lock()) {
                songsToRequeue.push_back(lastPlayed);
            }
            playbackHistory.pop();
            undoneCount++;
        }
        
        if (undoneCount > 0) {
             // Re-queue songs in the order they were played
            for (auto it = songsToRequeue.rbegin(); it != songsToRequeue.rend(); ++it) {
                addSongToPlaylist((*it)->title, (*it)->artist, (*it)->duration);
            }
            cout << "Re-queued " << undoneCount << " song(s) to the playlist." << endl;
        } else {
            cout << "No plays to undo." << endl;
        }
    }

    // --- Song Library (HashMap & BST) ---
    void findSong(const string& query) {
        if (allSongs.count(query)) {
            cout << "Found by ID: ";
            allSongs[query]->print();
            cout << endl;
            return;
        }
        string lowerQuery = query;
        transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
        vector<shared_ptr<Song>> foundSongs;
        for (const auto& pair : allSongs) {
            string lowerTitle = pair.second->title;
            transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
            if (lowerTitle.find(lowerQuery) != string::npos) {
                foundSongs.push_back(pair.second);
            }
        }
        if (foundSongs.empty()) {
            cout << "No song found matching '" << query << "'." << endl;
        } else {
            cout << "Found by Title (" << foundSongs.size() << " matches):" << endl;
            for (const auto& song : foundSongs) {
                cout << "  - ";
                song->print();
                cout << endl;
            }
        }
    }

    void updateSongRating(const string& songId, int rating) {
        if (rating < 1 || rating > 5) {
            cerr << "Error: Rating must be between 1 and 5." << endl;
            return;
        }
        if (allSongs.find(songId) == allSongs.end()) {
            cerr << "Error: Song ID not found." << endl;
            return;
        }
        
        auto song = allSongs[songId];
        if (song->rating > 0) {
            auto& bucket = songRatings[song->rating];
            bucket.erase(remove_if(bucket.begin(), bucket.end(), 
                [&](const weak_ptr<Song>& p) { return p.expired() || p.lock()->id == songId; }), bucket.end());
        }
        
        song->rating = rating;
        songRatings[rating].push_back(song);
        cout << "Updated rating for "; song->print(); cout << endl;
    }
    
    void findSongsByRating(int rating) {
        cout << "\n--- Songs with rating " << rating << " ---" << endl;
        if (songRatings.find(rating) == songRatings.end() || songRatings[rating].empty()) {
            cout << "  (none)" << endl;
            return;
        }
        for (const auto& weakSong : songRatings[rating]) {
            if (auto song = weakSong.lock()) {
                cout << "  - "; song->print(); cout << endl;
            }
        }
    }

    // --- Sorting & Shuffling ---
    void sortPlaylist(const string& criteria) {
        vector<shared_ptr<Song>> songs = getPlaylistAsVector();
        std::function<bool(const shared_ptr<Song>&, const shared_ptr<Song>&)> comparator;
        if (criteria == "title") {
            comparator = [](const auto& a, const auto& b) { return a->title < b->title; };
        } else if (criteria == "duration") {
            comparator = [](const auto& a, const auto& b) { return a->duration < b->duration; };
        } else if (criteria == "date") {
            comparator = [](const auto& a, const auto& b) { return a->dateAdded < b->dateAdded; };
        } else if (criteria == "rating") {
            comparator = [](const auto& a, const auto& b) { return a->rating > b->rating; }; // Descending
        } else {
            cerr << "Invalid sort criteria." << endl;
            return;
        }
        sort(songs.begin(), songs.end(), comparator);
        rebuildPlaylistFromVector(songs);
        cout << "Playlist sorted by " << criteria << "." << endl;
    }

    // Advanced Sorting Algorithms Implementation
    void mergeSortByTitle() {
        vector<shared_ptr<Song>> songs = getPlaylistAsVector();
        mergeSortHelper(songs, 0, songs.size() - 1, "title");
        rebuildPlaylistFromVector(songs);
        cout << "Playlist sorted by title using Merge Sort." << endl;
    }

    void mergeSortByDuration() {
        vector<shared_ptr<Song>> songs = getPlaylistAsVector();
        mergeSortHelper(songs, 0, songs.size() - 1, "duration");
        rebuildPlaylistFromVector(songs);
        cout << "Playlist sorted by duration using Merge Sort." << endl;
    }

    void quickSortByArtist() {
        vector<shared_ptr<Song>> songs = getPlaylistAsVector();
        quickSortHelper(songs, 0, songs.size() - 1, "artist");
        rebuildPlaylistFromVector(songs);
        cout << "Playlist sorted by artist using Quick Sort." << endl;
    }

    void quickSortByRating() {
        vector<shared_ptr<Song>> songs = getPlaylistAsVector();
        quickSortHelper(songs, 0, songs.size() - 1, "rating");
        rebuildPlaylistFromVector(songs);
        cout << "Playlist sorted by rating using Quick Sort." << endl;
    }

private:
    void mergeSortHelper(vector<shared_ptr<Song>>& arr, int left, int right, const string& criteria) {
        if (left < right) {
            int mid = left + (right - left) / 2;
            mergeSortHelper(arr, left, mid, criteria);
            mergeSortHelper(arr, mid + 1, right, criteria);
            mergeArrays(arr, left, mid, right, criteria);
        }
    }

    void mergeArrays(vector<shared_ptr<Song>>& arr, int left, int mid, int right, const string& criteria) {
        int n1 = mid - left + 1;
        int n2 = right - mid;
        
        vector<shared_ptr<Song>> leftArr(n1), rightArr(n2);
        
        for (int i = 0; i < n1; i++)
            leftArr[i] = arr[left + i];
        for (int j = 0; j < n2; j++)
            rightArr[j] = arr[mid + 1 + j];
        
        int i = 0, j = 0, k = left;
        
        while (i < n1 && j < n2) {
            bool shouldTakeLeft = false;
            if (criteria == "title") {
                shouldTakeLeft = leftArr[i]->title <= rightArr[j]->title;
            } else if (criteria == "duration") {
                shouldTakeLeft = leftArr[i]->duration <= rightArr[j]->duration;
            }
            
            if (shouldTakeLeft) {
                arr[k] = leftArr[i];
                i++;
            } else {
                arr[k] = rightArr[j];
                j++;
            }
            k++;
        }
        
        while (i < n1) {
            arr[k] = leftArr[i];
            i++;
            k++;
        }
        
        while (j < n2) {
            arr[k] = rightArr[j];
            j++;
            k++;
        }
    }

    void quickSortHelper(vector<shared_ptr<Song>>& arr, int low, int high, const string& criteria) {
        if (low < high) {
            int pi = partition(arr, low, high, criteria);
            quickSortHelper(arr, low, pi - 1, criteria);
            quickSortHelper(arr, pi + 1, high, criteria);
        }
    }

    int partition(vector<shared_ptr<Song>>& arr, int low, int high, const string& criteria) {
        auto pivot = arr[high];
        int i = (low - 1);
        
        for (int j = low; j <= high - 1; j++) {
            bool shouldSwap = false;
            if (criteria == "artist") {
                shouldSwap = arr[j]->artist < pivot->artist;
            } else if (criteria == "rating") {
                shouldSwap = arr[j]->rating > pivot->rating; // Descending for ratings
            }
            
            if (shouldSwap) {
                i++;
                swap(arr[i], arr[j]);
            }
        }
        swap(arr[i + 1], arr[high]);
        return (i + 1);
    }

public:
    void shufflePlaylist() {
        vector<shared_ptr<Song>> songs = getPlaylistAsVector();
        random_device rd;
        mt19937 g(rd());
        shuffle(songs.begin(), songs.end(), g);

        for (size_t i = 1; i < songs.size(); ++i) {
            if (songs[i]->artist == songs[i-1]->artist) {
                size_t j = i + 1;
                while (j < songs.size() && songs[j]->artist == songs[i]->artist) { j++; }
                if (j < songs.size()) { swap(songs[i], songs[j]); }
            }
        }
        rebuildPlaylistFromVector(songs);
        cout << "Playlist shuffled." << endl;
    }
    
    // --- System Snapshot ---
    void printSystemSnapshot() {
        cout << "\n======= SYSTEM SNAPSHOT =======" << endl;
        vector<shared_ptr<Song>> allSongsVec;
        for(auto const& [id, song] : allSongs) { allSongsVec.push_back(song); }
        sort(allSongsVec.begin(), allSongsVec.end(), [](const auto& a, const auto& b){ return a->duration > b->duration; });
        cout << "--- Top 5 Longest Songs ---" << endl;
        for(int i=0; i<min((int)allSongsVec.size(), 5); ++i) {
            cout << "  " << i+1 << ". "; allSongsVec[i]->print(); cout << endl;
        }
        cout << "\n--- Recently Played ---" << endl;
        if(playbackHistory.empty()) { cout << "  (none)" << endl; }
        else {
            auto tempStack = playbackHistory;
            int count = 0;
            while(!tempStack.empty() && count < 5) {
                if(auto song = tempStack.top().lock()) { cout << "  - "; song->print(); cout << endl; }
                tempStack.pop();
                count++;
            }
        }
        cout << "\n--- Song Count by Rating ---" << endl;
        for(int i=5; i>=1; --i) {
            size_t count = songRatings.count(i) ? songRatings[i].size() : 0;
            cout << "  " << i << " Stars: " << count << " songs" << endl;
        }
        cout << "=============================\n" << endl;
    }

    friend class AddCommand;
    friend class DeleteCommand;
    friend class MoveCommand;

public: 
    Node* getNodeByIndex(int index) {
        if (index < 1 || index > playlistCount) return nullptr;
        Node* current = head.get();
        for (int i = 1; i < index; ++i) { current = current->next.get(); }
        return current;
    }

    void removeSongInternal(const string& songId) {
        Node* prev = nullptr;
        Node* current = head.get();
        while(current) {
            if(auto s = current->song.lock(); s && s->id == songId) {
                unique_ptr<Node> nodeToDelete;
                if(prev) {
                    nodeToDelete = move(prev->next);
                    prev->next = move(nodeToDelete->next);
                    if(prev->next) prev->next->prev = prev; else tail = prev;
                } else {
                    nodeToDelete = move(head);
                    head = move(nodeToDelete->next);
                    if(head) head->prev = nullptr; else tail = nullptr;
                }
                playlistCount--;
                allSongs.erase(songId);
                return;
            }
            prev = current;
            current = current->next.get();
        }
    }
    
    shared_ptr<Song> removeSongAtIndex(int index) {
        Node* node = getNodeByIndex(index);
        if(!node) return nullptr;
        auto song = node->song.lock();
        removeSongInternal(song->id);
        return song;
    }

    void insertSongAt(shared_ptr<Song> song, int index) {
        if (!song) return;
        allSongs[song->id] = song;
        auto newNode = make_unique<Node>();
        newNode->song = song;
        
        if (index <= 1) {
            newNode->next = move(head);
            if(newNode->next) newNode->next->prev = newNode.get();
            head = move(newNode);
            if(!tail) tail = head.get();
        } else if (index > playlistCount) {
            tail->next = move(newNode);
            tail->next->prev = tail;
            tail = tail->next.get();
        } else {
            Node* prevNode = getNodeByIndex(index - 1);
            newNode->next = move(prevNode->next);
            newNode->prev = prevNode;
            if(newNode->next) newNode->next->prev = newNode.get();
            prevNode->next = move(newNode);
        }
        playlistCount++;
    }

    vector<shared_ptr<Song>> getPlaylistAsVector() {
        vector<shared_ptr<Song>> songs;
        songs.reserve(playlistCount);
        Node* current = head.get();
        while(current) {
            if(auto s = current->song.lock()) songs.push_back(s);
            current = current->next.get();
        }
        return songs;
    }

    void rebuildPlaylistFromVector(const vector<shared_ptr<Song>>& songs) {
        head.reset();
        tail = nullptr;
        playlistCount = 0;
        for (const auto& song : songs) {
             auto newNode = make_unique<Node>();
             newNode->song = song;
             newNode->prev = tail;
             if(!head) {
                 head = move(newNode);
                 tail = head.get();
             } else {
                 tail->next = move(newNode);
                 tail = tail->next.get();
             }
             playlistCount++;
        }
    }
};

void AddCommand::undo() { engine.removeSongInternal(songId); }
void DeleteCommand::undo() { engine.insertSongAt(deletedSong, originalIndex); }
void MoveCommand::undo() {
    auto song = engine.removeSongAtIndex(toIndex);
    engine.insertSongAt(song, fromIndex);
}
// ==============================================================================
// 4. Main Function (Interactive CLI)
// ==============================================================================
void printMenu() {
    cout << "\n--- PlayWise CLI Menu ---\n"
         << "  add                                      - Add a song\n"
         << "  delete <index>                           - Delete a song\n"
         << "  move <from_idx> <to_idx>                 - Move a song\n"
         << "  print                                    - Print playlist\n"
         << "  reverse                                  - Reverse playlist\n"
         << "  play <index | ID | \"Title\">              - Play a song\n"
         << "  hist_undo                                - Undo last play\n"
         << "  hist_undo_n <N>                          - Undo last N plays\n"
         << "  find <ID | \"Title\">                      - Find a song\n"
         << "  rate <index> <1-5>                       - Rate a song\n"
         << "  find_rate <1-5>                          - Find by rating\n"
         << "  sort <title|duration|date|rating>        - Sort playlist (STL)\n"
         << "  merge_title                              - Merge Sort by title\n"
         << "  merge_duration                           - Merge Sort by duration\n"
         << "  quick_artist                             - Quick Sort by artist\n"
         << "  quick_rating                             - Quick Sort by rating\n"
         << "  shuffle                                  - Shuffle playlist\n"
         << "  undo [N]                                 - Undo last N edit(s)\n"
         << "  snapshot                                 - Show system snapshot\n"
         << "  quit                                     - Exit\n"
         << "---------------------------\n"
         << "Enter command: ";
}

void clear_cin() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

int main() {
    PlayWiseEngine engine;
    engine.addSongToPlaylist("Bohemian Rhapsody", "Queen", 355);
    engine.addSongToPlaylist("Stairway to Heaven", "Led Zeppelin", 482);
    engine.addSongToPlaylist("Hotel California", "Eagles", 390);
    engine.printPlaylist();

    string cmd;
    while (true) {
        printMenu();
        cin >> cmd;
        if (cin.fail()) { clear_cin(); continue; }
        
        if (cmd == "quit") break;
        
        if (cmd == "add") {
            string title, artist, duration_str, line;
            cout << "Enter Title | Artist | Duration (e.g., 260 or 4:20): ";
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            getline(cin, line);
            stringstream ss(line);
            getline(ss, title, '|');
            getline(ss, artist, '|');
            getline(ss, duration_str);

            title.erase(0, title.find_first_not_of(" \t")); title.erase(title.find_last_not_of(" \t") + 1);
            artist.erase(0, artist.find_first_not_of(" \t")); artist.erase(artist.find_last_not_of(" \t") + 1);
            duration_str.erase(0, duration_str.find_first_not_of(" \t")); duration_str.erase(duration_str.find_last_not_of(" \t") + 1);

            int total_seconds = 0;
            size_t colon_pos = duration_str.find(':');
            try {
                if (colon_pos != string::npos) {
                    int minutes = stoi(duration_str.substr(0, colon_pos));
                    int seconds = stoi(duration_str.substr(colon_pos + 1));
                    total_seconds = (minutes * 60) + seconds;
                } else {
                    total_seconds = stoi(duration_str);
                }
                engine.addSongToPlaylist(title, artist, total_seconds);
            } catch (const std::exception& e) {
                cerr << "Invalid duration format. Please use seconds (260) or MM:SS (4:20)." << endl;
            }
        } else if (cmd == "delete") {
            int index; cin >> index; if(cin.fail()){ clear_cin(); continue; }
            engine.deleteSongFromPlaylist(index);
        } else if (cmd == "move") {
            int from, to; cin >> from >> to; if(cin.fail()){ clear_cin(); continue; }
            engine.moveSong(from, to);
        } else if (cmd == "print") {
            engine.printPlaylist();
        } else if (cmd == "reverse") {
            engine.reversePlaylist();
        } else if (cmd == "play") {
            string input;
            char next_char = cin.peek();
            if (isspace(next_char)) cin.ignore();
            getline(cin, input);
            
            try {
                int index = stoi(input);
                engine.playSong(index);
            } catch (const std::exception& e) {
                if (input.front() == '"' && input.back() == '"') {
                    input = input.substr(1, input.length() - 2);
                }
                engine.playSong(input);
            }
        } else if (cmd == "hist_undo") {
            engine.undoLastPlay();
        } else if (cmd == "hist_undo_n") {
            int steps; cin >> steps; if(cin.fail()){ clear_cin(); continue; }
            engine.undoLastPlays(steps);
        } else if (cmd == "find") {
            string query;
            char next_char = cin.peek();
            if (isspace(next_char)) cin.ignore();
            getline(cin, query);
            if (query.front() == '"' && query.back() == '"') {
                query = query.substr(1, query.length() - 2);
            }
            engine.findSong(query);
        } else if (cmd == "rate") {
            int index, rating; cin >> index >> rating; if(cin.fail()){ clear_cin(); continue; }
            auto node = engine.getNodeByIndex(index);
            if(node) {
                if(auto song = node->song.lock()) {
                    engine.updateSongRating(song->id, rating);
                }
            } else {
                cout << "Invalid song index." << endl;
            }
        } else if (cmd == "find_rate") {
            int rating; cin >> rating; if(cin.fail()){ clear_cin(); continue; }
            engine.findSongsByRating(rating);
        } else if (cmd == "sort") {
            string criteria; cin >> criteria;
            engine.sortPlaylist(criteria);
        } else if (cmd == "merge_title") {
            engine.mergeSortByTitle();
        } else if (cmd == "merge_duration") {
            engine.mergeSortByDuration();
        } else if (cmd == "quick_artist") {
            engine.quickSortByArtist();
        } else if (cmd == "quick_rating") {
            engine.quickSortByRating();
        } else if (cmd == "shuffle") {
            engine.shufflePlaylist();
        } else if (cmd == "undo") {
            int steps = 1;
            while (isspace(cin.peek())) {
                cin.ignore();
            }
            if (isdigit(cin.peek())) {
                cin >> steps;
            }
            engine.undoLastEdit(steps);
        } else if (cmd == "snapshot") {
            engine.printSystemSnapshot();
        } else {
            cout << "Unknown command." << endl;
        }
    }

    return 0;
}