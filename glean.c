#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define MAX_FILENAME_LENGTH 512
#define MAX_URL_LENGTH 2048
#define MAX_COMMAND_LENGTH 4096
#define USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"
#define REDDIT_API_BASE "https://www.reddit.com/r/"
#define MAX_POSTS_PER_REQUEST 100
#define DEFAULT_LIMIT 25
#define MAX_RETRIES 3
#define RETRY_DELAY 5

struct MemoryStruct {
    char *memory;
    size_t size;
};

char* str_replace(const char* string, const char* substr, const char* replacement) {
    char* tok = NULL;
    char* newstr = NULL;
    char* oldstr = NULL;
    int   oldstr_len = 0;
    int   substr_len = 0;
    int   replacement_len = 0;

    newstr = strdup(string);
    substr_len = strlen(substr);
    replacement_len = strlen(replacement);

    if (substr == NULL || replacement == NULL) {
        return newstr;
    }

    while ((tok = strstr(newstr, substr))) {
        oldstr = newstr;
        oldstr_len = strlen(oldstr);
        newstr = (char*)malloc(sizeof(char) * (oldstr_len - substr_len + replacement_len + 1));

        if (newstr == NULL) {
            free(oldstr);
            return NULL;
        }

        memcpy(newstr, oldstr, tok - oldstr);
        memcpy(newstr + (tok - oldstr), replacement, replacement_len);
        memcpy(newstr + (tok - oldstr) + replacement_len, tok + substr_len, oldstr_len - substr_len - (tok - oldstr));
        memset(newstr + oldstr_len - substr_len + replacement_len, 0, 1);

        free(oldstr);
    }

    return newstr;
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Error: Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

void create_directory(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    if (strlen(path) >= PATH_MAX) {
        fprintf(stderr, "Error: Path too long\n");
        return;
    }

    strncpy(tmp, path, PATH_MAX - 1);
    tmp[PATH_MAX - 1] = '\0';
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

int is_reddit_video(const char *url) {
    return (strstr(url, "v.redd.it") != NULL);
}

int is_youtube_video(const char *url) {
    return (strstr(url, "youtube.com") != NULL || strstr(url, "youtu.be") != NULL);
}

int is_image(const char *url) {
    const char *ext = strrchr(url, '.');
    if (ext) {
        return (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
                strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".gif") == 0);
    }
    return 0;
}

int file_exists(const char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

void sanitize_filename(char *filename) {
    const char *invalid_chars = "\\/:*?\"<>|";
    for (char *p = filename; *p; p++) {
        if (strchr(invalid_chars, *p)) {
            *p = '_';
        }
    }
}

void download_video(const char *video_url, const char *subreddit, const char *download_dir, const char *custom_filename) {
    char command[MAX_COMMAND_LENGTH];
    char output_path[PATH_MAX];
    char sanitized_filename[PATH_MAX];
    
    strncpy(sanitized_filename, custom_filename, sizeof(sanitized_filename) - 1);
    sanitized_filename[sizeof(sanitized_filename) - 1] = '\0';
    sanitize_filename(sanitized_filename);
    
    int path_length = snprintf(output_path, sizeof(output_path), "%s/%s", download_dir, subreddit);
    if (path_length < 0 || (size_t)path_length >= sizeof(output_path)) {
        fprintf(stderr, "Error: Path too long\n");
        return;
    }
    create_directory(output_path);
    
    size_t remaining_space = sizeof(output_path) - (size_t)path_length - 1;
    int filename_length = snprintf(output_path + path_length, remaining_space, "/%s", sanitized_filename);
    if (filename_length < 0 || (size_t)filename_length >= remaining_space) {
        fprintf(stderr, "Error: Filename too long\n");
        return;
    }
    
    if (file_exists(output_path)) {
        printf("File already exists, skipping: %s\n", output_path);
        return;
    }
    
    if (snprintf(command, sizeof(command), 
                 "yt-dlp -f 'bestvideo+bestaudio/best' --merge-output-format mp4 "
                 "--add-header 'User-Agent:%s' "
                 "-o \"%s\" \"%s\"", 
                 USER_AGENT, output_path, video_url) >= (int)sizeof(command)) {
        fprintf(stderr, "Error: Command too long\n");
        return;
    }
    
    printf("Downloading video: %s\n", video_url);
    int result = system(command);
    
    if (result == 0) {
        printf("Video downloaded successfully: %s\n", output_path);
    } else {
        fprintf(stderr, "Failed to download video: %s\n", video_url);
    }
}

void download_image(CURL *curl, const char *image_url, const char *subreddit, const char *download_dir, const char *custom_filename) {
    FILE *fp;
    char output_path[PATH_MAX];
    char sanitized_filename[PATH_MAX];
    
    strncpy(sanitized_filename, custom_filename, sizeof(sanitized_filename) - 1);
    sanitized_filename[sizeof(sanitized_filename) - 1] = '\0';
    sanitize_filename(sanitized_filename);
    
    int path_length = snprintf(output_path, sizeof(output_path), "%s/%s", download_dir, subreddit);
    if (path_length < 0 || (size_t)path_length >= sizeof(output_path)) {
        fprintf(stderr, "Error: Path too long\n");
        return;
    }
    create_directory(output_path);
    
    size_t remaining_space = sizeof(output_path) - (size_t)path_length - 1;
    int filename_length = snprintf(output_path + path_length, remaining_space, "/%s", sanitized_filename);
    if (filename_length < 0 || (size_t)filename_length >= remaining_space) {
        fprintf(stderr, "Error: Filename too long\n");
        return;
    }
    
    if (file_exists(output_path)) {
        printf("File already exists, skipping: %s\n", output_path);
        return;
    }
    
    fp = fopen(output_path, "wb");
    if (fp) {
        curl_easy_setopt(curl, CURLOPT_URL, image_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        
        printf("Downloading image: %s\n", image_url);
        CURLcode res = curl_easy_perform(curl);
        fclose(fp);
        
        if (res == CURLE_OK) {
            printf("Image downloaded successfully: %s\n", output_path);
        } else {
            fprintf(stderr, "Failed to download image: %s\n", image_url);
            remove(output_path);
        }
    } else {
        fprintf(stderr, "Failed to create file: %s\n", output_path);
    }
}

json_object* fetch_posts(CURL *curl_handle, const char *subreddit, const char *category, int limit, const char *after, const char *time_filter) {
    char url[MAX_URL_LENGTH];
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    if (strcmp(category, "top") == 0) {
        snprintf(url, sizeof(url), "%s%s/%s.json?limit=%d&t=%s%s%s", REDDIT_API_BASE, subreddit, category, limit, time_filter, after ? "&after=" : "", after ? after : "");
    } else {
        snprintf(url, sizeof(url), "%s%s/%s.json?limit=%d%s%s", REDDIT_API_BASE, subreddit, category, limit, after ? "&after=" : "", after ? after : "");
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        free(chunk.memory);
        return NULL;
    }

    json_object *json = json_tokener_parse(chunk.memory);
    free(chunk.memory);

    return json;
}

int process_posts(json_object *json, const char *subreddit, int *processed_posts, int limit, int download_media, const char *download_dir, char *after_token, CURL *curl_handle, const char *file_scheme, int min_score, int verbose) {
    json_object *data, *children, *child, *post_data, *after;
    int posts_in_page = 0;
    
    json_object_object_get_ex(json, "data", &data);
    json_object_object_get_ex(data, "children", &children);
    
    int num_posts = json_object_array_length(children);
    
    for (int i = 0; i < num_posts && *processed_posts < limit; i++) {
        child = json_object_array_get_idx(children, i);
        json_object_object_get_ex(child, "data", &post_data);
        
        json_object *title, *score, *url, *id, *author, *created_utc;
        json_object_object_get_ex(post_data, "title", &title);
        json_object_object_get_ex(post_data, "score", &score);
        json_object_object_get_ex(post_data, "url", &url);
        json_object_object_get_ex(post_data, "id", &id);
        json_object_object_get_ex(post_data, "author", &author);
        json_object_object_get_ex(post_data, "created_utc", &created_utc);
        
        const char *title_str = json_object_get_string(title);
        int score_int = json_object_get_int(score);
        const char *url_str = json_object_get_string(url);
        const char *id_str = json_object_get_string(id);
        const char *author_str = json_object_get_string(author);
        time_t post_date = json_object_get_int64(created_utc);
        
        if (verbose) {
            printf("Post %d:\n", *processed_posts + 1);
            printf("Title: %s\n", title_str);
            printf("Author: %s\n", author_str);
            printf("Score: %d\n", score_int);
            printf("URL: %s\n", url_str);
        }
        
        if (download_media && score_int >= min_score) {
            char custom_filename[MAX_FILENAME_LENGTH];
            const char *file_extension = ".mp4";  // Default to .mp4 for videos
            
            if (is_image(url_str)) {
                file_extension = strrchr(url_str, '.');
                if (!file_extension) {
                    file_extension = ".jpg";  // Default to .jpg if no extension found for images
                }
            }
            
            char date_str[20];
            strftime(date_str, sizeof(date_str), "%Y%m%d", localtime(&post_date));
            
            if (file_scheme) {
                char temp_filename[MAX_FILENAME_LENGTH];
                snprintf(temp_filename, sizeof(temp_filename), file_scheme, score_int, author_str, id_str, date_str);
                // Replace placeholders with actual values
                char *result = temp_filename;
                char score_str[20];
                snprintf(score_str, sizeof(score_str), "%d", score_int);
                result = str_replace(result, "{UPVOTES}", score_str);
                result = str_replace(result, "{REDDITOR}", author_str);
                result = str_replace(result, "{POSTID}", id_str);
                result = str_replace(result, "{DATE}", date_str);
                snprintf(custom_filename, sizeof(custom_filename), "%s%s", result, file_extension);
                free(result);
            } else {
                snprintf(custom_filename, sizeof(custom_filename), "%s_%s%s", id_str, author_str, file_extension);
            }
            
            if (is_reddit_video(url_str) || is_youtube_video(url_str)) {
                download_video(url_str, subreddit, download_dir, custom_filename);
            } else if (is_image(url_str)) {
                download_image(curl_handle, url_str, subreddit, download_dir, custom_filename);
            } else if (verbose) {
                printf("Unsupported media type: %s\n", url_str);
            }
        }
        
        (*processed_posts)++;
        posts_in_page++;
        if (verbose) printf("\n");
    }
    
    json_object_object_get_ex(data, "after", &after);
    if (after && json_object_get_string(after)) {
        strncpy(after_token, json_object_get_string(after), 128 - 1);
        after_token[128 - 1] = '\0';
    } else {
        after_token[0] = '\0';
    }

    return posts_in_page;
}

void download_posts(const char *subreddit, const char *category, int limit, int download_media, const char *download_dir, const char *file_scheme, int min_score, const char *time_filter, int verbose) {
    CURL *curl_handle;
    char after_token[128] = "";
    int processed_posts = 0;
    int fetch_limit = (limit < MAX_POSTS_PER_REQUEST) ? limit : MAX_POSTS_PER_REQUEST;
    int retry_count = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, USER_AGENT);

        while (processed_posts < limit) {
            json_object *json = fetch_posts(curl_handle, subreddit, category, fetch_limit, after_token[0] ? after_token : NULL, time_filter);
            if (!json) {
                if (retry_count < MAX_RETRIES) {
                    fprintf(stderr, "Error: Failed to fetch posts. Retrying in %d seconds...\n", RETRY_DELAY);
                    sleep(RETRY_DELAY);
                    retry_count++;
                    continue;
                } else {
                    fprintf(stderr, "Error: Failed to fetch posts after %d retries. Exiting.\n", MAX_RETRIES);
                    break;
                }
            }

            retry_count = 0;  // Reset retry count on successful fetch
            int posts_in_page = process_posts(json, subreddit, &processed_posts, limit, download_media, download_dir, after_token, curl_handle, file_scheme, min_score, verbose);
            json_object_put(json);

            if (posts_in_page == 0 || !after_token[0]) {
                if (verbose) printf("No more posts to fetch.\n");
                break;
            }

            if (verbose) printf("Fetched %d posts. Continuing to next page...\n", processed_posts);
            sleep(2); // Add a small delay between requests to avoid rate limiting
        }

        curl_easy_cleanup(curl_handle);
    }

    curl_global_cleanup();
}

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s, --subreddit <subreddit>  Specify the subreddit (required)\n");
    fprintf(stderr, "  -c, --category <category>    Specify the category (hot, new, top) (default: hot)\n");
    fprintf(stderr, "  -l, --limit <number>         Specify the number of posts to fetch (default: %d)\n", DEFAULT_LIMIT);
    fprintf(stderr, "  -d, --download-media         Download media files (videos and images)\n");
    fprintf(stderr, "  -o, --output-dir <directory> Specify the download directory (default: reddit_downloads)\n");
    fprintf(stderr, "  -f, --file-scheme <scheme>   Specify the file naming scheme (default: {POSTID}_{REDDITOR})\n");
    fprintf(stderr, "  -m, --min-score <number>     Specify the minimum score for posts to download\n");
    fprintf(stderr, "  -t, --time-filter <filter>   Specify the time filter for 'top' category (hour, day, week, month, year, all)\n");
    fprintf(stderr, "  -v, --verbose                Enable verbose output\n");
    fprintf(stderr, "  -h, --help                   Display this help message\n");
}

int main(int argc, char *argv[]) {
    char *subreddit = NULL;
    char *category = "hot";
    int limit = DEFAULT_LIMIT;
    int download_media = 0;
    char *download_dir = "reddit_downloads";
    char *file_scheme = NULL;
    int min_score = 0;
    char *time_filter = "all";
    int verbose = 0;
    int opt;

    static struct option long_options[] = {
        {"subreddit", required_argument, 0, 's'},
        {"category", required_argument, 0, 'c'},
        {"limit", required_argument, 0, 'l'},
        {"download-media", no_argument, 0, 'd'},
        {"output-dir", required_argument, 0, 'o'},
        {"file-scheme", required_argument, 0, 'f'},
        {"min-score", required_argument, 0, 'm'},
        {"time-filter", required_argument, 0, 't'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "s:c:l:do:f:m:t:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                subreddit = optarg;
                break;
            case 'c':
                category = optarg;
                break;
            case 'l':
                limit = atoi(optarg);
                if (limit <= 0) {
                    fprintf(stderr, "Error: Limit must be a positive integer\n");
                    print_usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'd':
                download_media = 1;
                break;
            case 'o':
                download_dir = optarg;
                break;
            case 'f':
                file_scheme = optarg;
                break;
            case 'm':
                min_score = atoi(optarg);
                break;
            case 't':
                time_filter = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (!subreddit) {
        fprintf(stderr, "Error: Subreddit is required\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (download_media) {
        create_directory(download_dir);
    }

    if (verbose) {
        printf("Fetching posts from r/%s (%s), limit: %d\n", subreddit, category, limit);
        if (download_media) {
            printf("Media downloads enabled\n");
            printf("Download directory: %s\n", download_dir);
            if (file_scheme) {
                printf("File naming scheme: %s\n", file_scheme);
            }
        }
        if (min_score > 0) {
            printf("Minimum score: %d\n", min_score);
        }
        if (strcmp(category, "top") == 0) {
            printf("Time filter: %s\n", time_filter);
        }
        printf("\n");
    }

    download_posts(subreddit, category, limit, download_media, download_dir, file_scheme, min_score, time_filter, verbose);

    return EXIT_SUCCESS;
}
