#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class file_dialog
{
public:
    enum class mode {
        open_file,
        save_file
    };

    struct request {
        mode operation = mode::open_file;
        std::string title;
        std::string confirm_label;
        std::filesystem::path initial_path;
        std::string filter_label;
        std::vector<std::string> extensions;
        std::string default_extension;
        bool show_recent = false;
    };

    struct result {
        bool accepted = false;
        std::filesystem::path path;
    };

    void set_recent_storage_path(const std::filesystem::path &path);
    void open(request request);
    std::optional<result> render();
    void remember_recent(const std::filesystem::path &path);

private:
    struct directory_entry {
        std::filesystem::path path;
        std::string name;
        bool is_directory = false;
        std::uintmax_t size = 0;
    };

    void initialise_location(const std::filesystem::path &path);
    bool set_directory(const std::filesystem::path &path);
    void refresh_directory();
    bool matches_filter(const std::filesystem::path &path) const;
    std::filesystem::path selected_path() const;
    std::optional<result> try_accept(bool overwrite);
    void create_folder();
    void load_recent_files();
    void save_recent_files() const;

    request request_;
    std::filesystem::path current_directory_;
    std::filesystem::path recent_storage_path_;
    std::vector<directory_entry> entries_;
    std::vector<std::filesystem::path> recent_files_;
    std::string popup_id_;
    std::string location_text_;
    std::string filename_;
    std::string new_folder_name_;
    std::string error_;
    bool pending_open_ = false;
    bool active_ = false;
    bool overwrite_confirmation_ = false;
    bool show_new_folder_ = false;
    bool recents_loaded_ = false;
};
