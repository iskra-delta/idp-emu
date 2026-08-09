#include "file_dialog.hpp"
#include "chrome_metrics.hpp"
#include "chrome_theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace {
namespace fs = std::filesystem;

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

fs::path absolute_normal(const fs::path &path)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec)
        absolute = path;
    return absolute.lexically_normal();
}

std::string file_size_text(std::uintmax_t bytes)
{
    char text[32]{};
    if (bytes >= 1024u * 1024u * 1024u)
        std::snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024u * 1024u)
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024u)
        std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    return text;
}

bool draw_dialog_header(const std::string &title)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const float window_width = ImGui::GetWindowWidth();
    const float header_height = chrome_metrics::title_bar_height;
    const float close_size = header_height - 4.0f;
    const ImVec2 close_min(window_pos.x + window_width - close_size - 2.0f,
                           window_pos.y + 2.0f);
    const ImVec2 close_max(close_min.x + close_size, close_min.y + close_size);

    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(window_pos,
                        ImVec2(window_pos.x + window_width, window_pos.y + header_height),
                        chrome_theme().title_bg,
                        style.WindowRounding,
                        ImDrawFlags_RoundCornersTop);

    constexpr float dot_radius = 1.35f;
    for (int column = 0; column < 2; ++column)
    {
        for (int row = 0; row < 3; ++row)
        {
            draw->AddCircleFilled(
                ImVec2(window_pos.x + 7.0f + column * 4.0f,
                       window_pos.y + 9.0f + row * 4.0f),
                dot_radius,
                chrome_theme().chrome_handle,
                6);
        }
    }

    const ImVec2 title_size = ImGui::CalcTextSize(title.c_str());
    draw->AddText(ImVec2(window_pos.x + 20.0f,
                         window_pos.y + (header_height - title_size.y) * 0.5f),
                  chrome_theme().title_text,
                  title.c_str());

    ImGui::SetCursorScreenPos(window_pos);
    ImGui::InvisibleButton("##FileDialogDrag",
                           ImVec2(std::max(1.0f, window_width - close_size - 4.0f),
                                  header_height));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(window_pos.x + delta.x, window_pos.y + delta.y),
                            ImGuiCond_Always);
    }

    ImGui::SetCursorScreenPos(close_min);
    const bool close_clicked =
        ImGui::InvisibleButton("##FileDialogClose", ImVec2(close_size, close_size));
    if (ImGui::IsItemHovered())
        draw->AddRectFilled(close_min, close_max, chrome_theme().close_hover_bg, 3.0f);

    const float x_padding = close_size * 0.29f;
    draw->AddLine(ImVec2(close_min.x + x_padding, close_min.y + x_padding),
                  ImVec2(close_max.x - x_padding, close_max.y - x_padding),
                  chrome_theme().close_x_color,
                  1.5f);
    draw->AddLine(ImVec2(close_max.x - x_padding, close_min.y + x_padding),
                  ImVec2(close_min.x + x_padding, close_max.y - x_padding),
                  chrome_theme().close_x_color,
                  1.5f);
    draw->AddLine(ImVec2(window_pos.x, window_pos.y + header_height),
                  ImVec2(window_pos.x + window_width, window_pos.y + header_height),
                  chrome_theme().chrome_border,
                  1.0f);

    ImGui::SetCursorScreenPos(
        ImVec2(window_pos.x + style.WindowPadding.x,
               window_pos.y + header_height + style.WindowPadding.y));
    return close_clicked;
}
}

void file_dialog::set_recent_storage_path(const std::filesystem::path &path)
{
    recent_storage_path_ = path;
    recents_loaded_ = false;
    load_recent_files();
}

void file_dialog::open(request request)
{
    request_ = std::move(request);
    if (request_.title.empty())
        request_.title = request_.operation == mode::open_file ? "Open File" : "Save File";
    if (request_.confirm_label.empty())
        request_.confirm_label = request_.operation == mode::open_file ? "Open" : "Save";

    for (std::string &extension : request_.extensions)
    {
        if (!extension.empty() && extension.front() != '.')
            extension.insert(extension.begin(), '.');
        extension = lower_copy(extension);
    }
    if (!request_.default_extension.empty() && request_.default_extension.front() != '.')
        request_.default_extension.insert(request_.default_extension.begin(), '.');
    request_.default_extension = lower_copy(request_.default_extension);

    popup_id_ = request_.title + "###IDPFileDialog";
    filename_.clear();
    new_folder_name_.clear();
    error_.clear();
    overwrite_confirmation_ = false;
    show_new_folder_ = false;
    initialise_location(request_.initial_path);
    pending_open_ = true;
    active_ = true;
}

void file_dialog::initialise_location(const std::filesystem::path &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path initial = path.empty() ? fs::current_path(ec) : absolute_normal(path);
    if (ec)
        initial = ".";

    ec.clear();
    if (fs::is_directory(initial, ec) && !ec)
    {
        set_directory(initial);
        return;
    }

    filename_ = initial.filename().string();
    fs::path parent = initial.parent_path();
    if (parent.empty())
        parent = fs::current_path(ec);
    if (!set_directory(parent))
    {
        ec.clear();
        set_directory(fs::current_path(ec));
    }
}

bool file_dialog::set_directory(const std::filesystem::path &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path candidate = absolute_normal(path);
    if (!fs::exists(candidate, ec) || ec)
    {
        error_ = "That folder does not exist.";
        return false;
    }
    if (!fs::is_directory(candidate, ec) || ec)
    {
        error_ = "That location is not a folder.";
        return false;
    }

    current_directory_ = candidate;
    location_text_ = candidate.string();
    error_.clear();
    refresh_directory();
    return true;
}

void file_dialog::refresh_directory()
{
    namespace fs = std::filesystem;
    entries_.clear();
    std::error_code ec;
    fs::directory_iterator iterator(current_directory_, fs::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        error_ = "Could not read this folder: " + ec.message();
        return;
    }

    const fs::directory_iterator end;
    for (; iterator != end; iterator.increment(ec))
    {
        if (ec)
        {
            ec.clear();
            continue;
        }

        const fs::directory_entry &item = *iterator;
        std::error_code item_ec;
        const bool is_directory = item.is_directory(item_ec);
        if (item_ec)
            continue;
        if (!is_directory && !item.is_regular_file(item_ec))
            continue;
        if (item_ec || (!is_directory && !matches_filter(item.path())))
            continue;

        directory_entry entry;
        entry.path = item.path();
        entry.name = item.path().filename().string();
        entry.is_directory = is_directory;
        if (!is_directory)
        {
            entry.size = item.file_size(item_ec);
            if (item_ec)
                entry.size = 0;
        }
        entries_.push_back(std::move(entry));
    }

    std::sort(entries_.begin(), entries_.end(),
              [](const directory_entry &a, const directory_entry &b) {
                  if (a.is_directory != b.is_directory)
                      return a.is_directory > b.is_directory;
                  return lower_copy(a.name) < lower_copy(b.name);
              });
}

bool file_dialog::matches_filter(const std::filesystem::path &path) const
{
    if (request_.extensions.empty())
        return true;
    const std::string extension = lower_copy(path.extension().string());
    return std::find(request_.extensions.begin(), request_.extensions.end(), extension) !=
           request_.extensions.end();
}

std::filesystem::path file_dialog::selected_path() const
{
    namespace fs = std::filesystem;
    fs::path path(filename_);
    if (!path.is_absolute())
        path = current_directory_ / path;
    if (request_.operation == mode::save_file && !path.has_extension() &&
        !request_.default_extension.empty())
        path += request_.default_extension;
    return path.lexically_normal();
}

std::optional<file_dialog::result> file_dialog::try_accept(bool overwrite)
{
    namespace fs = std::filesystem;
    if (filename_.empty())
    {
        error_ = "Choose a file name.";
        return std::nullopt;
    }

    const fs::path path = selected_path();
    if (!matches_filter(path))
    {
        error_ = request_.filter_label.empty()
                     ? "Choose a supported file."
                     : "Choose a file matching " + request_.filter_label + ".";
        return std::nullopt;
    }

    std::error_code ec;
    const bool exists = fs::exists(path, ec);
    if (ec)
    {
        error_ = "Could not inspect that path: " + ec.message();
        return std::nullopt;
    }

    if (request_.operation == mode::open_file)
    {
        if (!exists || !fs::is_regular_file(path, ec) || ec)
        {
            error_ = "Choose an existing file.";
            return std::nullopt;
        }
    }
    else
    {
        const fs::path parent = path.parent_path();
        if (parent.empty() || !fs::is_directory(parent, ec) || ec)
        {
            error_ = "Choose an existing destination folder.";
            return std::nullopt;
        }
        if (exists && fs::is_directory(path, ec))
        {
            error_ = "A folder already has that name.";
            return std::nullopt;
        }
        if (exists && !overwrite)
        {
            overwrite_confirmation_ = true;
            error_.clear();
            return std::nullopt;
        }
    }

    overwrite_confirmation_ = false;
    error_.clear();
    return result{true, path};
}

void file_dialog::create_folder()
{
    namespace fs = std::filesystem;
    if (new_folder_name_.empty())
    {
        error_ = "Enter a folder name.";
        return;
    }

    const fs::path path = (current_directory_ / new_folder_name_).lexically_normal();
    if (path.parent_path() != current_directory_)
    {
        error_ = "Enter a folder name, not a path.";
        return;
    }

    std::error_code ec;
    if (!fs::create_directory(path, ec))
    {
        error_ = ec ? "Could not create the folder: " + ec.message()
                    : "A file or folder already has that name.";
        return;
    }

    new_folder_name_.clear();
    show_new_folder_ = false;
    set_directory(path);
}

std::optional<file_dialog::result> file_dialog::render()
{
    namespace fs = std::filesystem;
    if (!active_)
        return std::nullopt;

    if (pending_open_)
    {
        ImGui::OpenPopup(popup_id_.c_str());
        pending_open_ = false;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    ImVec2 work_size = viewport->WorkSize;
    const ImVec2 owner_center = viewport->GetCenter();

    // Size against the monitor work area rather than the emulator window.
    // This matters when the file browser owns a detached platform viewport.
    const ImGuiPlatformIO &platform = ImGui::GetPlatformIO();
    for (const ImGuiPlatformMonitor &monitor : platform.Monitors)
    {
        const ImVec2 monitor_max(monitor.MainPos.x + monitor.MainSize.x,
                                 monitor.MainPos.y + monitor.MainSize.y);
        if (owner_center.x >= monitor.MainPos.x && owner_center.x < monitor_max.x &&
            owner_center.y >= monitor.MainPos.y && owner_center.y < monitor_max.y)
        {
            work_pos = monitor.WorkPos;
            work_size = monitor.WorkSize;
            break;
        }
    }

    const ImVec2 dialog_size(
        std::min(980.0f, std::max(320.0f, work_size.x - 48.0f)),
        std::min(640.0f, std::max(300.0f, work_size.y - 64.0f)));
    ImVec2 dialog_pos(owner_center.x - dialog_size.x * 0.5f,
                      owner_center.y - dialog_size.y * 0.5f);
    dialog_pos.x = std::clamp(dialog_pos.x, work_pos.x,
                              work_pos.x + std::max(0.0f, work_size.x - dialog_size.x));
    dialog_pos.y = std::clamp(dialog_pos.y, work_pos.y,
                              work_pos.y + std::max(0.0f, work_size.y - dialog_size.y));

    const bool detached =
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    if (detached)
    {
        ImGuiWindowClass window_class;
        window_class.ParentViewportId = viewport->ID;
        window_class.ViewportFlagsOverrideSet =
            ImGuiViewportFlags_NoAutoMerge | ImGuiViewportFlags_NoDecoration;
        ImGui::SetNextWindowClass(&window_class);
    }

    ImGui::SetNextWindowPos(dialog_pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(
        dialog_size,
        ImGuiCond_Appearing);
    const ImGuiWindowFlags dialog_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove;
    if (!ImGui::BeginPopupModal(popup_id_.c_str(), nullptr, dialog_flags))
    {
        if (!ImGui::IsPopupOpen(popup_id_.c_str()))
        {
            active_ = false;
            return result{false, {}};
        }
        return std::nullopt;
    }

    std::optional<result> outcome;
    if (draw_dialog_header(request_.title))
        outcome = result{false, {}};

    const float toolbar_height =
        ImGui::GetFrameHeightWithSpacing() * 2.0f +
        ImGui::GetStyle().WindowPadding.y * 2.0f + 4.0f;
    ImGui::BeginChild("##FileDialogToolbar", ImVec2(0.0f, toolbar_height), true);
    const fs::path parent = current_directory_.parent_path();
    ImGui::BeginDisabled(parent.empty() || parent == current_directory_);
    if (ImGui::Button("Up"))
        set_directory(parent);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Working Folder"))
    {
        std::error_code ec;
        set_directory(fs::current_path(ec));
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        refresh_directory();

    if (request_.operation == mode::save_file)
    {
        ImGui::SameLine();
        if (!show_new_folder_)
        {
            if (ImGui::Button("New Folder"))
            {
                show_new_folder_ = true;
                new_folder_name_.clear();
            }
        }
        else
        {
            char folder_name[256]{};
            std::snprintf(folder_name, sizeof(folder_name), "%s", new_folder_name_.c_str());
            ImGui::SetNextItemWidth(220.0f);
            const bool create_on_enter = ImGui::InputText("##NewFolderName", folder_name, sizeof(folder_name),
                                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemEdited())
                new_folder_name_ = folder_name;
            ImGui::SameLine();
            if (ImGui::Button("Create") || create_on_enter)
                create_folder();
            ImGui::SameLine();
            if (ImGui::Button("Cancel##NewFolder"))
                show_new_folder_ = false;
        }
    }

    char location[2048]{};
    std::snprintf(location, sizeof(location), "%s", location_text_.c_str());
    ImGui::TextUnformatted("Location");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-55.0f);
    const bool location_entered = ImGui::InputText("##FileDialogLocation", location, sizeof(location),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited())
        location_text_ = location;
    ImGui::SameLine();
    if (ImGui::Button("Go") || location_entered)
        set_directory(location_text_);
    ImGui::EndChild();

    const float browser_height = std::max(220.0f, ImGui::GetContentRegionAvail().y - 112.0f);
    if (request_.show_recent)
    {
        ImGui::BeginChild("##RecentFiles", ImVec2(230.0f, browser_height), true);
        ImGui::TextUnformatted("Recent Files");
        ImGui::Separator();
        bool any_recent = false;
        for (const fs::path &recent : recent_files_)
        {
            std::error_code ec;
            if (!fs::is_regular_file(recent, ec) || ec || !matches_filter(recent))
                continue;
            any_recent = true;
            const std::string path_text = recent.string();
            const std::string name = recent.filename().string();
            ImGui::PushID(path_text.c_str());
            if (ImGui::Selectable(name.c_str(), selected_path() == recent,
                                  ImGuiSelectableFlags_AllowDoubleClick))
            {
                set_directory(recent.parent_path());
                filename_ = recent.filename().string();
                overwrite_confirmation_ = false;
                error_.clear();
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    outcome = try_accept(false);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", path_text.c_str());
            ImGui::PopID();
        }
        if (!any_recent)
            ImGui::TextDisabled("No recent files yet.");
        ImGui::EndChild();
        ImGui::SameLine();
    }

    ImGui::BeginChild("##FileBrowser", ImVec2(-1.0f, browser_height), true);
    if (ImGui::BeginTable("##FileEntries", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0f, browser_height - 16.0f)))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        fs::path directory_to_open;
        for (const directory_entry &entry : entries_)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string id = entry.path.string();
            const std::string label = (entry.is_directory ? "[Folder]  " : "") + entry.name;
            ImGui::PushID(id.c_str());
            const bool selected = !entry.is_directory && filename_ == entry.name;
            if (ImGui::Selectable(label.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (entry.is_directory)
                {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        directory_to_open = entry.path;
                }
                else
                {
                    filename_ = entry.name;
                    overwrite_confirmation_ = false;
                    error_.clear();
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                        request_.operation == mode::open_file)
                        outcome = try_accept(false);
                }
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            if (!entry.is_directory)
                ImGui::TextDisabled("%s", file_size_text(entry.size).c_str());
        }
        ImGui::EndTable();
        if (!directory_to_open.empty())
            set_directory(directory_to_open);
    }
    ImGui::EndChild();

    char filename[1024]{};
    std::snprintf(filename, sizeof(filename), "%s", filename_.c_str());
    ImGui::SetNextItemWidth(-180.0f);
    const bool filename_entered = ImGui::InputText("File name", filename, sizeof(filename),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited())
    {
        filename_ = filename;
        overwrite_confirmation_ = false;
        error_.clear();
    }
    if (!request_.filter_label.empty())
        ImGui::TextDisabled("Files: %s", request_.filter_label.c_str());

    if (!error_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 90, 90, 255));
        ImGui::TextWrapped("%s", error_.c_str());
        ImGui::PopStyleColor();
    }
    else if (overwrite_confirmation_)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 180, 75, 255));
        ImGui::TextWrapped("%s already exists. Replace it?", selected_path().filename().string().c_str());
        ImGui::PopStyleColor();
    }

    if (overwrite_confirmation_)
    {
        if (ImGui::Button("Replace"))
            outcome = try_accept(true);
        ImGui::SameLine();
        if (ImGui::Button("Choose Another Name"))
            overwrite_confirmation_ = false;
    }
    else if (ImGui::Button(request_.confirm_label.c_str()) || filename_entered)
    {
        outcome = try_accept(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##FileDialog"))
        outcome = result{false, {}};

    if (outcome)
    {
        active_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return outcome;
}

void file_dialog::remember_recent(const std::filesystem::path &path)
{
    namespace fs = std::filesystem;
    load_recent_files();
    const fs::path normalized = absolute_normal(path);
    recent_files_.erase(
        std::remove(recent_files_.begin(), recent_files_.end(), normalized),
        recent_files_.end());
    recent_files_.insert(recent_files_.begin(), normalized);
    constexpr std::size_t maximum_recent_files = 12;
    if (recent_files_.size() > maximum_recent_files)
        recent_files_.resize(maximum_recent_files);
    save_recent_files();
}

void file_dialog::load_recent_files()
{
    namespace fs = std::filesystem;
    if (recents_loaded_)
        return;
    recents_loaded_ = true;
    recent_files_.clear();
    if (recent_storage_path_.empty())
        return;

    std::ifstream input(recent_storage_path_);
    std::string value;
    while (input >> std::quoted(value))
    {
        const fs::path path = absolute_normal(value);
        if (std::find(recent_files_.begin(), recent_files_.end(), path) == recent_files_.end())
            recent_files_.push_back(path);
        if (recent_files_.size() == 12)
            break;
    }
}

void file_dialog::save_recent_files() const
{
    if (recent_storage_path_.empty())
        return;
    std::ofstream output(recent_storage_path_, std::ios::trunc);
    for (const std::filesystem::path &path : recent_files_)
        output << std::quoted(path.string()) << '\n';
}
