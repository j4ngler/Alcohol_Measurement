#include "FileServer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

// Tag for this component
static const char *TAG = "FileServer";

// External references (defined in main.c)
extern EventGroupHandle_t sampling_control_event;
extern TaskHandle_t getDataFromSensorTask_handle;
#define START_SAMPLING_BIT BIT1

/* Handler to redirect incoming GET request for /index.html to /
 * This can be overridden by uploading file with same name */
esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);  // Response body can be empty
    return ESP_OK;
}

/* Handler to respond with an icon file embedded in flash.
 * Browsers expect to GET website icon at URI /favicon.ico.
 * This can be overridden by uploading file with same name */
esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

/**
 * @brief Validate if a filename is safe to process
 * Filters out corrupted entries with invalid characters
 * @param name Filename to validate
 * @return true if valid, false otherwise
 */
static bool is_valid_filename(const char *name)
{
    if (name == NULL) {
        return false;
    }
    
    size_t len = strlen(name);
    
    // Check for empty name or too long name (FAT32 max is 255 chars)
    if (len == 0 || len > 255) {
        return false;
    }
    
    // Check for null bytes in the middle (indicates corruption)
    // strlen already checks this, but double-check
    for (size_t i = 0; i < len; i++) {
        // Check for control characters (0x00-0x1F except space, tab, newline)
        if (name[i] < 0x20 && name[i] != '\t' && name[i] != '\n' && name[i] != '\r') {
            return false; // Control character found (likely corruption)
        }
    }
    
    // Basic validation passed
    return true;
}

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
esp_err_t http_response_dir_html(httpd_req_t *req, const char *dirpath)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir) {
        ESP_LOGE(__func__, "Failed to stat dir : %s", dirpath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    /* Get handle to embedded file upload script */
    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    /* Send file-list table definition and column labels */
    httpd_resp_sendstr_chunk(req,
        "<table class=\"fixed\" border=\"1\">"
        "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" />"
        "<thead><tr><th>Name</th><th>Type</th><th>Size (Bytes)</th><th>Delete</th></tr></thead>"
        "<tbody>");

    /* Iterate over all files / folders and fetch their names and sizes */
    int skipped_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Validate filename before processing to filter corrupted entries
        if (!is_valid_filename(entry->d_name)) {
            skipped_count++;
            ESP_LOGD(__func__, "Skipping invalid/corrupted entry (invalid characters detected)");
            continue;
        }
        
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

        // Ensure entrypath has proper separator
        if (dirpath_len > 0 && entrypath[dirpath_len - 1] != '/') {
            entrypath[dirpath_len] = '/';
            entrypath[dirpath_len + 1] = '\0';
            strlcpy(entrypath + dirpath_len + 1, entry->d_name, sizeof(entrypath) - dirpath_len - 1);
        } else {
            strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        }
        
        if (stat(entrypath, &entry_stat) == -1) {
            // Only log as debug to reduce noise - corruption is expected for some entries
            ESP_LOGD(__func__, "Failed to stat %s (skipping): %s", entrytype, entry->d_name);
            skipped_count++;
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);
        ESP_LOGI(__func__, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

        /* Send chunk of HTML file containing table entries with file name and size */
        httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        if (entry->d_type == DT_DIR) {
            httpd_resp_sendstr_chunk(req, "/");
        }
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "</a></td><td>");
        httpd_resp_sendstr_chunk(req, entrytype);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, entrysize);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
        httpd_resp_sendstr_chunk(req, "</td></tr>\n");
    }
    closedir(dir);
    
    // Log summary of skipped entries
    if (skipped_count > 0) {
        ESP_LOGW(__func__, "Skipped %d corrupted/invalid entries", skipped_count);
    }

    /* Finish the file list table */
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* Set HTTP response content type according to file extension */
esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Handler to download a file kept on the server */
esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(__func__, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') {
        return http_response_dir_html(req, filepath);
    }

    if (stat(filepath, &file_stat) == -1) {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index.html") == 0) {
            return index_html_get_handler(req);
        } else if (strcmp(filename, "/favicon.ico") == 0) {
            return favicon_get_handler(req);
        }
        ESP_LOGE(__func__, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(__func__, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(__func__, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(__func__, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(__func__, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
#ifdef CONFIG_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Handler to delete a file from the server */
esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    /* Skip leading "/delete" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri  + sizeof("/delete") - 1, sizeof(filepath));
    if (!filename) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(__func__, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(__func__, "File does not exist : %s", filename);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(__func__, "Deleting file : %s", filename);
    /* Delete file */
    unlink(filepath);

    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

/* API handler to start sampling */
esp_err_t api_start_sampling_handler(httpd_req_t *req)
{
    extern EventGroupHandle_t sampling_control_event;
    
    if (sampling_control_event != NULL) {
        xEventGroupSetBits(sampling_control_event, START_SAMPLING_BIT);
        ESP_LOGI(TAG, "✅ Sampling started via HTTP API");
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Sampling started\"}");
    } else {
        ESP_LOGE(TAG, "❌ Sampling control event not initialized");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"System not ready\"}");
    }
    return ESP_OK;
}

/* API handler to stop sampling */
esp_err_t api_stop_sampling_handler(httpd_req_t *req)
{
    extern TaskHandle_t getDataFromSensorTask_handle;
    
    // Note: Stopping is handled by the sampling task itself after completion
    ESP_LOGI(TAG, "📊 Stop command received (sampling will complete current cycle)");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"info\",\"message\":\"Sampling will complete current cycle\"}");
    return ESP_OK;
}

/* API handler to get system status */
esp_err_t api_status_handler(httpd_req_t *req)
{
    extern TaskHandle_t getDataFromSensorTask_handle;
    
    char status_json[256];
    bool is_sampling = (getDataFromSensorTask_handle != NULL);
    
    snprintf(status_json, sizeof(status_json),
             "{\"status\":\"ok\",\"sampling\":%s,\"message\":\"System ready\"}",
             is_sampling ? "true" : "false");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, status_json);
    return ESP_OK;
}

/* API handler to update dashboard configuration */
esp_err_t api_config_dashboard_handler(httpd_req_t *req)
{
    char content[256];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(content)) {
        ESP_LOGE(TAG, "Content too long");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, content, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    ESP_LOGI(TAG, "Received dashboard config: %s (length: %d)", content, ret);
    
    // Parse JSON: {"host":"192.168.1.100","port":3000}
    // Simple JSON parsing (cJSON would be better but keeping it simple)
    char *host_start = strstr(content, "\"host\"");
    char *port_start = strstr(content, "\"port\"");
    
    if (!host_start || !port_start) {
        ESP_LOGE(TAG, "Invalid JSON format - missing host or port");
        ESP_LOGE(TAG, "Expected format: {\"host\":\"IP\",\"port\":PORT}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid JSON format - missing host or port\"}");
        return ESP_FAIL;
    }
    
    // Extract host - look for value after "host":
    char *host_quote = strchr(host_start + 6, '"');
    if (!host_quote) {
        ESP_LOGE(TAG, "Invalid host format - missing opening quote");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid host format\"}");
        return ESP_FAIL;
    }
    char *host_end = strchr(host_quote + 1, '"');
    if (!host_end) {
        ESP_LOGE(TAG, "Invalid host format - missing closing quote");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid host format\"}");
        return ESP_FAIL;
    }
    int host_len = host_end - host_quote - 1;
    if (host_len <= 0 || host_len >= 64) {
        ESP_LOGE(TAG, "Invalid host length: %d", host_len);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid host length\"}");
        return ESP_FAIL;
    }
    char host[64];
    memcpy(host, host_quote + 1, host_len);
    host[host_len] = '\0';
    
    // Extract port - look for number after "port":
    char *port_colon = strchr(port_start + 6, ':');
    if (!port_colon) {
        // Try without colon (direct number)
        port_colon = port_start + 6;
        while (*port_colon && (*port_colon == ' ' || *port_colon == ':')) port_colon++;
    } else {
        port_colon++; // Skip colon
        while (*port_colon && *port_colon == ' ') port_colon++; // Skip spaces
    }
    
    if (!port_colon || *port_colon == '\0') {
        ESP_LOGE(TAG, "Invalid port format - no number found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid port format\"}");
        return ESP_FAIL;
    }
    
    int port = atoi(port_colon);
    if (port <= 0 || port > 65535) {
        ESP_LOGE(TAG, "Invalid port number: %d", port);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid port number (must be 1-65535)\"}");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Parsed dashboard config: host='%s', port=%d", host, port);
    
    // Save to NVS
    extern esp_err_t save_dashboard_config_to_nvs(const char *host, int port);
    extern void trigger_dashboard_registration(void);  // From main.c
    esp_err_t err = save_dashboard_config_to_nvs(host, port);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✅ Dashboard config updated successfully");
        
        // Trigger re-registration with new config
        trigger_dashboard_registration();
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Dashboard config updated\",\"host\":\"");
        httpd_resp_sendstr_chunk(req, host);
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "\",\"port\":%d}", port);
        httpd_resp_sendstr_chunk(req, port_str);
        httpd_resp_sendstr_chunk(req, NULL);
    } else {
        ESP_LOGE(TAG, "❌ Failed to save dashboard config to NVS: %s (0x%x)", esp_err_to_name(err), err);
        httpd_resp_set_type(req, "application/json");
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "{\"status\":\"error\",\"message\":\"Failed to save config: %s\"}", esp_err_to_name(err));
        httpd_resp_sendstr(req, error_msg);
    }
    
    return ESP_OK;
}

/* Handler to serve config page */
esp_err_t config_page_handler(httpd_req_t *req)
{
    const char *html = 
        "<!DOCTYPE html>"
        "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Dashboard Config</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;background:#f5f5f5;}"
        "h1{color:#333;text-align:center;}"
        ".form-group{margin-bottom:20px;}"
        "label{display:block;margin-bottom:5px;font-weight:bold;color:#555;}"
        "input[type='text'],input[type='number']{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;font-size:16px;box-sizing:border-box;}"
        "button{background:#4CAF50;color:white;padding:12px 24px;border:none;border-radius:4px;cursor:pointer;font-size:16px;width:100%;}"
        "button:hover{background:#45a049;}"
        ".message{margin-top:20px;padding:10px;border-radius:4px;display:none;}"
        ".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}"
        ".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}"
        "</style></head><body>"
        "<h1>⚙️ ESP32 Configuration</h1>"
        "<form id='configForm'>"
        "<h2>📡 Dashboard Server</h2>"
        "<div class='form-group'>"
        "<label for='host'>Dashboard Server IP:</label>"
        "<input type='text' id='host' name='host' placeholder='192.168.1.100' required>"
        "</div>"
        "<div class='form-group'>"
        "<label for='port'>Port:</label>"
        "<input type='number' id='port' name='port' placeholder='3000' min='1' max='65535' value='3000' required>"
        "</div>"
        "<h2>🌐 Static IP Configuration</h2>"
        "<div class='form-group'>"
        "<label><input type='checkbox' id='staticIpEnabled' onchange='toggleStaticIp()'> Enable Static IP (to prevent IP changes)</label>"
        "</div>"
        "<div id='staticIpFields' style='display:none;'>"
        "<div class='form-group'>"
        "<label for='staticIp'>ESP32 IP Address:</label>"
        "<input type='text' id='staticIp' name='staticIp' placeholder='192.168.0.122' pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$'>"
        "</div>"
        "<div class='form-group'>"
        "<label for='staticNetmask'>Netmask:</label>"
        "<input type='text' id='staticNetmask' name='staticNetmask' placeholder='255.255.255.0' pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$'>"
        "</div>"
        "<div class='form-group'>"
        "<label for='staticGateway'>Gateway:</label>"
        "<input type='text' id='staticGateway' name='staticGateway' placeholder='192.168.0.1' pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$'>"
        "</div>"
        "</div>"
        "<button type='submit'>💾 Save Configuration</button>"
        "</form>"
        "<div id='message' class='message'></div>"
        "<script>"
        "function toggleStaticIp(){"
        "const enabled=document.getElementById('staticIpEnabled').checked;"
        "document.getElementById('staticIpFields').style.display=enabled?'block':'none';"
        "}"
        "document.getElementById('configForm').addEventListener('submit',async function(e){"
        "e.preventDefault();"
        "const host=document.getElementById('host').value;"
        "const port=parseInt(document.getElementById('port').value);"
        "const staticIpEnabled=document.getElementById('staticIpEnabled').checked;"
        "const staticIp=document.getElementById('staticIp').value;"
        "const staticNetmask=document.getElementById('staticNetmask').value;"
        "const staticGateway=document.getElementById('staticGateway').value;"
        "const messageDiv=document.getElementById('message');"
        "messageDiv.style.display='none';"
        "try{"
        "const dashboardResponse=await fetch('/api/config/dashboard',{"
        "method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({host:host,port:port})"
        "});"
        "const dashboardData=await dashboardResponse.json();"
        "if(dashboardData.status!=='success'){"
        "messageDiv.className='message error';"
        "messageDiv.textContent='❌ Dashboard config error: '+dashboardData.message;"
        "messageDiv.style.display='block';"
        "return;"
        "}"
        "if(staticIpEnabled){"
        "if(!staticIp||!staticNetmask||!staticGateway){"
        "messageDiv.className='message error';"
        "messageDiv.textContent='❌ Please fill all static IP fields';"
        "messageDiv.style.display='block';"
        "return;"
        "}"
        "const staticIpResponse=await fetch('/api/config/staticip',{"
        "method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({enabled:true,ip:staticIp,netmask:staticNetmask,gateway:staticGateway})"
        "});"
        "const staticIpData=await staticIpResponse.json();"
        "if(staticIpData.status!=='success'){"
        "messageDiv.className='message error';"
        "messageDiv.textContent='❌ Static IP config error: '+staticIpData.message;"
        "messageDiv.style.display='block';"
        "return;"
        "}"
        "messageDiv.className='message success';"
        "messageDiv.textContent='✅ Configuration saved! Please restart ESP32 for static IP to take effect.';"
        "}else{"
        "const staticIpResponse=await fetch('/api/config/staticip',{"
        "method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({enabled:false})"
        "});"
        "messageDiv.className='message success';"
        "messageDiv.textContent='✅ Configuration saved successfully!';"
        "}"
        "messageDiv.style.display='block';"
        "if(messageDiv.className.includes('success')){setTimeout(()=>location.reload(),3000);}"
        "}catch(error){"
        "messageDiv.className='message error';"
        "messageDiv.textContent='❌ Network error: '+error.message;"
        "messageDiv.style.display='block';"
        "}"
        "});"
        "</script></body></html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/* Function to start the file server */
esp_err_t start_file_server(const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    if (server_data) {
        ESP_LOGE(__func__, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(__func__, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(__func__, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(__func__, "Failed to start file server!");
        return ESP_FAIL;
    }

    /* IMPORTANT: Register specific routes BEFORE wildcard routes to avoid conflicts */
    
    /* Handler for config page - must be registered before wildcard */
    httpd_uri_t config_page = {
        .uri       = "/config",
        .method    = HTTP_GET,
        .handler   = config_page_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &config_page);

    /* API handler for starting sampling */
    httpd_uri_t api_start = {
        .uri       = "/api/start",
        .method    = HTTP_POST,
        .handler   = api_start_sampling_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &api_start);

    /* API handler for stopping sampling */
    httpd_uri_t api_stop = {
        .uri       = "/api/stop",
        .method    = HTTP_POST,
        .handler   = api_stop_sampling_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &api_stop);

    /* API handler for getting status */
    httpd_uri_t api_status = {
        .uri       = "/api/status",
        .method    = HTTP_GET,
        .handler   = api_status_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &api_status);

    /* API handler for updating dashboard config */
    httpd_uri_t api_config_dashboard = {
        .uri       = "/api/config/dashboard",
        .method    = HTTP_POST,
        .handler   = api_config_dashboard_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &api_config_dashboard);

    /* API handler for updating static IP config */
    httpd_uri_t api_config_static_ip = {
        .uri       = "/api/config/staticip",
        .method    = HTTP_POST,
        .handler   = api_config_static_ip_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &api_config_static_ip);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri       = "/delete/*",   // Match all URIs of type /delete/path/to/file
        .method    = HTTP_POST,
        .handler   = delete_post_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_delete);

    /* URI handler for getting uploaded files - MUST be registered LAST (wildcard) */
    httpd_uri_t file_download = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    return ESP_OK;
}
