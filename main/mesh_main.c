#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/temperature_sensor.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"



/*******************************************************
 *                Constants
 *******************************************************/
#define RX_SIZE (1500)
#define TX_SIZE (1460)



/*******************************************************
 *                Variable Definitions
 *******************************************************/

char* ap_ip = NULL;
char* ap_mac = NULL;
char* ap_par = NULL;

temperature_sensor_handle_t temp_sensor = NULL;
temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);

static const char* TAG = "teste";
static const char* MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static uint8_t tx_buf[TX_SIZE] = {0};
static uint8_t rx_buf[RX_SIZE] = {0};
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_netif_t* netif_sta = NULL;



/*******************************************************
 *                Function Definitions
 *******************************************************/

char* float_to_s(float _num) {
  int len = snprintf(NULL, 0, "%.2f", _num) + 1;
  char* str = malloc(len);
  snprintf(str, len, "%.2f", _num);
  return str;
}

void http_rest_with_url(char* post_data) {
  esp_err_t err = ESP_OK;
  ESP_LOGI(TAG, "post_data: %s", post_data);

  esp_http_client_config_t http_client_config = {
      .host = "192.168.15.11",
      .port = 8000,
      .path = "/graphs/temp",
      .disable_auto_redirect = true,
      .method = HTTP_METHOD_POST,
  };

  esp_http_client_handle_t client = esp_http_client_init(&http_client_config);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));
  err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64,
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
}

// void esp_mesh_p2p_tx_main(void* arg) {
//   int i;
//   int send_count = 0;
//   mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
//   int route_table_size = 0;
//   mesh_data_t data;
//   data.data = tx_buf;
//   data.size = sizeof(tx_buf);
//   data.proto = MESH_PROTO_BIN;
//   data.tos = MESH_TOS_P2P;
//   is_running = true;
//   while (is_running) {
//     /* non-root do nothing but print */
//     if (!esp_mesh_is_root()) {
//       ESP_LOGI(MESH_TAG, "layer:%d, rtableSize:%d, %s", mesh_layer,
//                esp_mesh_get_routing_table_size(),
//                is_mesh_connected ? "NODE" : "DISCONNECT");
//       vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
//       continue;
//     }
//     esp_mesh_get_routing_table((mesh_addr_t*)&route_table,
//                                CONFIG_MESH_ROUTE_TABLE_SIZE * 6,
//                                &route_table_size);
//     if (send_count && !(send_count % 100)) {
//       ESP_LOGI(MESH_TAG, "size:%d/%d,send_count:%d", route_table_size,
//                esp_mesh_get_routing_table_size(), send_count);
//     }
//     for (i = 0; i < route_table_size; i++) {
//       esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
//     }
//     if (route_table_size < 10) {
//       vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
//     }
//   }
//   vTaskDelete(NULL);
// }

// void esp_mesh_p2p_rx_main(void* arg) {
//   // int recv_count = 0;
//   esp_err_t err;
//   mesh_addr_t from;
//   int send_count = 0;
//   mesh_data_t data;
//   int flag = 0;
//   data.data = rx_buf;
//   data.size = RX_SIZE;
//   is_running = true;
//   while (is_running) {
//     data.size = RX_SIZE;
//     err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
//     if (err != ESP_OK || !data.size) {
//       ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
//       continue;
//     }
//     /* extract send count */
//     if (data.size >= sizeof(send_count)) {
//       send_count = (data.data[25] << 24) | (data.data[24] << 16) |
//                    (data.data[23] << 8) | data.data[22];
//     }
//     // recv_count++;
//     // /* process light control */
//     // mesh_light_process(&from, data.data, data.size);
//     // if (!(recv_count % 1)) {
//     //   ESP_LOGW(
//     //       MESH_TAG,
//     //       "[#RX:%d/%d][L:%d] parent:" MACSTR ", receive from " MACSTR
//     //       ", size:%d, heap:%" PRId32 ", flag:%d[err:0x%x, proto:%d, tos:%d]",
//     //       recv_count, send_count, mesh_layer, MAC2STR(mesh_parent_addr.addr),
//     //       MAC2STR(from.addr), data.size, esp_get_minimum_free_heap_size(), flag,
//     //       err, data.proto, data.tos);
//     // }
//   }
//   vTaskDelete(NULL);
// }

// // receber pacotes da rede externa e enviar internamente
// void tx_root_task(void* param) {
//   // uint8_t rx_buf[RX_SIZE] = {0};
//   // variavies para rotear pacotes
//   mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];  // tabela de roteamento
//   int route_table_size = 0;           // tamanho da tabela de roteamento
//   mesh_data_t data;                   // dados a serem enviados
//   data.data = rx_buf;
//   data.size = RX_SIZE;
//   // variaveis para identificar a fonte do pacote recebido
//   struct sockaddr_storage source_addr;
//   socklen_t socklen = sizeof(source_addr);
//   // configurando endereco do servidor local
//   struct sockaddr_in dest_addr = {
//       .sin_addr.s_addr = inet_addr("0.0.0.0"),
//       .sin_family = AF_INET,
//       .sin_port = htons(9998),
//   };
//   // criar socket e vincula-lo ao ip:porta deste node
//   int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
//   bind(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
//   while (1) {
//     recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
//              (struct sockaddr*)&source_addr, &socklen);  // esperar mensagem
//     ESP_LOGI(MESH_TAG, "tx root %s", rx_buf);  // imprime buffer nos logs
//     // preencher tabela de roteamento com endereco MAC dos nodes filhos
//     esp_mesh_get_routing_table((mesh_addr_t*)&route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6,
//                                &route_table_size);
//     // iterar pela tabela e enviar dados obtidos de aplicacao externa para todos os nodes
//     for (int i = 0; i < route_table_size; i++)
//       esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
//     memset(rx_buf, 0, sizeof(rx_buf));  // limpar buffer para novo uso
//   }
//   vTaskDelete(NULL);
// }

// receber pacotes internamente e enviar para rede externa
void rx_root_task(void* param) {
  char* post_data;

  int flag = 0;
  mesh_addr_t sender;
  mesh_data_t data;

  data.data = rx_buf;
  data.size = RX_SIZE;

  while (1) {
    if (esp_mesh_recv(&sender, &data, portMAX_DELAY, &flag, NULL, 0) == ESP_OK && ap_ip != NULL) {
      asprintf(&post_data, "{\"ip\":\"%s\",%s}", ap_ip, data.data); // prepara buffer para envio
      ESP_LOGI(MESH_TAG, "%s", post_data);                          // imprime buffer nos logs
      http_rest_with_url(post_data);
      free(post_data);
    }
  }
  vTaskDelete(NULL);
}

void tx_child_task(void* param) {
  if (ap_par == NULL) {
    ESP_LOGI(TAG, "PAR is NULL");
    return;
  }
  if (ap_mac == NULL) {
    ESP_LOGI(TAG, "MAC is NULL");
    return;
  }

  mesh_data_t data;
  float tsens_value;
  char* post_data;

  // temperatura do dispositivo
  ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));

  // parte dos dados json
  asprintf(&post_data, "\"pai\":\"%s\",\"endereco\":\"%s\",\"temperatura\":%s", ap_par, ap_mac, float_to_s(tsens_value));

  memcpy((uint8_t*)&tx_buf, post_data, strlen(post_data));
  data.data = tx_buf;
  data.size = TX_SIZE;

  while (1) {
    esp_mesh_send(NULL, &data, 0, NULL, 0);  // enviar mensagem para node principal
    vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}

void rx_child_task(void* param) {
  int flag = 0;
  mesh_addr_t sender;
  mesh_data_t data;

  data.data = rx_buf;
  data.size = RX_SIZE;

  while (1) {
    if (esp_mesh_recv(&sender, &data, portMAX_DELAY, &flag, NULL, 0) == ESP_OK)
      ESP_LOGI(MESH_TAG, "child remetente: " MACSTR ", msg: %s", MAC2STR(sender.addr), data.data);
  }
  vTaskDelete(NULL);
}

void check_health_task(void* param) {
  wifi_ap_record_t ap;
  mesh_vote_t vote = { .percentage = 0.9, .is_rc_specified = false, .config = {.attempts = 15} };

  while (1) {
    // parar autoconfiguracao da rede antes de chamar API do Wi-Fi
    ESP_ERROR_CHECK(esp_mesh_set_self_organized(false, false));

    // coletar informacoes do access point (roteador)
    esp_wifi_sta_get_ap_info(&ap);

    // retornando autoconfiguracao
    ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, false));

    ESP_LOGI(MESH_TAG, "rssi: %d", ap.rssi);
    if (ap.rssi < -10) {
      ESP_LOGI(MESH_TAG, "RSSI menor que -90, chamando nova eleicao!");
      esp_mesh_waive_root(&vote, MESH_VOTE_REASON_ROOT_INITIATED);
      break;
    }

    // vTaskDelay(pdMS_TO_TICKS(10000));
    vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}

esp_err_t esp_mesh_comm_p2p_start(void) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  ap_mac = malloc(18);
  snprintf(ap_mac, 18, MACSTR, MAC2STR(mac));

  ap_par = malloc(18);
  snprintf(ap_par, 18, MACSTR, MAC2STR(mesh_parent_addr.addr));

  xTaskCreate(tx_child_task, "TX_TASK", configMINIMAL_STACK_SIZE + 2048, NULL, configMAX_PRIORITIES - 3, NULL);

  if (esp_mesh_is_root()) {
    xTaskCreate(check_health_task, "HEALTH_TASK", configMINIMAL_STACK_SIZE + 1024, NULL, configMAX_PRIORITIES - 3, NULL);
    // xTaskCreate(tx_root_task, "TX_TASK", configMINIMAL_STACK_SIZE + 2048, NULL, configMAX_PRIORITIES - 3, NULL);
    xTaskCreate(rx_root_task, "RX_TASK", configMINIMAL_STACK_SIZE + 2048, NULL, configMAX_PRIORITIES - 3, NULL);
  } else {
    // xTaskCreate(rx_child_task, "RX_TASK", configMINIMAL_STACK_SIZE + 2048, NULL, configMAX_PRIORITIES - 3, NULL);
    printf(
        "\n=============================================="
        "\n== MAC address: %s"
        "\n== Parent MAC address: %s"
        "\n==============================================\n\n",
        ap_mac, ap_par);
  }

  return ESP_OK;
}

void mesh_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {
  mesh_addr_t id = { 0, };
  static uint16_t last_layer = 0;

  switch (event_id) {
    case MESH_EVENT_STARTED: {
      esp_mesh_get_id(&id);
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:" MACSTR "",
               MAC2STR(id.addr));
      is_mesh_connected = false;
      mesh_layer = esp_mesh_get_layer();
    } break;
    case MESH_EVENT_STOPPED: {
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
      is_mesh_connected = false;
      mesh_layer = esp_mesh_get_layer();
    } break;
    case MESH_EVENT_CHILD_CONNECTED: {
      mesh_event_child_connected_t* child_connected =
          (mesh_event_child_connected_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, " MACSTR "",
               child_connected->aid, MAC2STR(child_connected->mac));
    } break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
      mesh_event_child_disconnected_t* child_disconnected =
          (mesh_event_child_disconnected_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, " MACSTR "",
               child_disconnected->aid, MAC2STR(child_disconnected->mac));
    } break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
      mesh_event_routing_table_change_t* routing_table =
          (mesh_event_routing_table_change_t*)event_data;
      ESP_LOGW(MESH_TAG,
               "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
               routing_table->rt_size_change, routing_table->rt_size_new,
               mesh_layer);
    } break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
      mesh_event_routing_table_change_t* routing_table =
          (mesh_event_routing_table_change_t*)event_data;
      ESP_LOGW(MESH_TAG,
               "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
               routing_table->rt_size_change, routing_table->rt_size_new,
               mesh_layer);
    } break;
    case MESH_EVENT_NO_PARENT_FOUND: {
      mesh_event_no_parent_found_t* no_parent =
          (mesh_event_no_parent_found_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
               no_parent->scan_times);
    }
    /* TODO handler for the failure */
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
      mesh_event_connected_t* connected = (mesh_event_connected_t*)event_data;
      esp_mesh_get_id(&id);
      mesh_layer = connected->self_layer;
      memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
      ESP_LOGI(MESH_TAG,
               "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:" MACSTR
               "%s, ID:" MACSTR ", duty:%d",
               last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
               esp_mesh_is_root()  ? "<ROOT>"
               : (mesh_layer == 2) ? "<layer2>"
                                   : "",
               MAC2STR(id.addr), connected->duty);
      last_layer = mesh_layer;
      // mesh_connected_indicator(mesh_layer);
      is_mesh_connected = true;
      if (esp_mesh_is_root()) {
        esp_netif_dhcpc_stop(netif_sta);
        esp_netif_dhcpc_start(netif_sta);
      }
      esp_mesh_comm_p2p_start();
    } break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
      mesh_event_disconnected_t* disconnected =
          (mesh_event_disconnected_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
               disconnected->reason);
      is_mesh_connected = false;
      // mesh_disconnected_indicator();
      mesh_layer = esp_mesh_get_layer();
    } break;
    case MESH_EVENT_LAYER_CHANGE: {
      mesh_event_layer_change_t* layer_change =
          (mesh_event_layer_change_t*)event_data;
      mesh_layer = layer_change->new_layer;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s", last_layer,
               mesh_layer,
               esp_mesh_is_root()  ? "<ROOT>"
               : (mesh_layer == 2) ? "<layer2>"
                                   : "");
      last_layer = mesh_layer;
      // mesh_connected_indicator(mesh_layer);
    } break;
    case MESH_EVENT_ROOT_ADDRESS: {
      mesh_event_root_address_t* root_addr =
          (mesh_event_root_address_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:" MACSTR "",
               MAC2STR(root_addr->addr));
    } break;
    case MESH_EVENT_VOTE_STARTED: {
      mesh_event_vote_started_t* vote_started =
          (mesh_event_vote_started_t*)event_data;
      ESP_LOGI(
          MESH_TAG,
          "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:" MACSTR "",
          vote_started->attempts, vote_started->reason,
          MAC2STR(vote_started->rc_addr.addr));
    } break;
    case MESH_EVENT_VOTE_STOPPED: {
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
      break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
      mesh_event_root_switch_req_t* switch_req =
          (mesh_event_root_switch_req_t*)event_data;
      ESP_LOGI(MESH_TAG,
               "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:" MACSTR "",
               switch_req->reason, MAC2STR(switch_req->rc_addr.addr));
    } break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
      /* new root */
      mesh_layer = esp_mesh_get_layer();
      esp_mesh_get_parent_bssid(&mesh_parent_addr);
      ESP_LOGI(MESH_TAG,
               "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:" MACSTR "",
               mesh_layer, MAC2STR(mesh_parent_addr.addr));
    } break;
    case MESH_EVENT_TODS_STATE: {
      mesh_event_toDS_state_t* toDs_state =
          (mesh_event_toDS_state_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    } break;
    case MESH_EVENT_ROOT_FIXED: {
      mesh_event_root_fixed_t* root_fixed =
          (mesh_event_root_fixed_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
               root_fixed->is_fixed ? "fixed" : "not fixed");
    } break;
    case MESH_EVENT_ROOT_ASKED_YIELD: {
      mesh_event_root_conflict_t* root_conflict =
          (mesh_event_root_conflict_t*)event_data;
      ESP_LOGI(MESH_TAG,
               "<MESH_EVENT_ROOT_ASKED_YIELD>" MACSTR ", rssi:%d, capacity:%d",
               MAC2STR(root_conflict->addr), root_conflict->rssi,
               root_conflict->capacity);
    } break;
    case MESH_EVENT_CHANNEL_SWITCH: {
      mesh_event_channel_switch_t* channel_switch =
          (mesh_event_channel_switch_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d",
               channel_switch->channel);
    } break;
    case MESH_EVENT_SCAN_DONE: {
      mesh_event_scan_done_t* scan_done = (mesh_event_scan_done_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d", scan_done->number);
    } break;
    case MESH_EVENT_NETWORK_STATE: {
      mesh_event_network_state_t* network_state =
          (mesh_event_network_state_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
               network_state->is_rootless);
    } break;
    case MESH_EVENT_STOP_RECONNECTION: {
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    } break;
    case MESH_EVENT_FIND_NETWORK: {
      mesh_event_find_network_t* find_network =
          (mesh_event_find_network_t*)event_data;
      ESP_LOGI(MESH_TAG,
               "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:" MACSTR
               "",
               find_network->channel, MAC2STR(find_network->router_bssid));
    } break;
    case MESH_EVENT_ROUTER_SWITCH: {
      mesh_event_router_switch_t* router_switch =
          (mesh_event_router_switch_t*)event_data;
      ESP_LOGI(MESH_TAG,
               "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, " MACSTR
               "",
               router_switch->ssid, router_switch->channel,
               MAC2STR(router_switch->bssid));
    } break;
    case MESH_EVENT_PS_PARENT_DUTY: {
      mesh_event_ps_duty_t* ps_duty = (mesh_event_ps_duty_t*)event_data;
      ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
    } break;
    case MESH_EVENT_PS_CHILD_DUTY: {
      mesh_event_ps_duty_t* ps_duty = (mesh_event_ps_duty_t*)event_data;
      ESP_LOGI(MESH_TAG,
               "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, " MACSTR ", duty:%d",
               ps_duty->child_connected.aid - 1,
               MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
    } break;
    default:
      ESP_LOGI(MESH_TAG, "unknown id:%" PRId32 "", event_id);
      break;
  }
}

void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                      void* event_data) {
  ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
  ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR,
           IP2STR(&event->ip_info.ip));

  int len = snprintf(NULL, 0, IPSTR, IP2STR(&event->ip_info.ip)) + 1;
  ap_ip = malloc(len);
  snprintf(ap_ip, len, IPSTR, IP2STR(&event->ip_info.ip));

  printf(
      "\n=============================================="
      "\n== IP address: %s"
      "\n== MAC address: %s"
      "\n== Parent MAC address: %s"
      "\n==============================================\n\n",
      ap_ip, ap_mac, ap_par);
}



void app_main(void) {
  // Inicializa sensor de temperatura
  ESP_LOGI(TAG,
           "Install temperature sensor, expected temp ranger range: 10~50 ℃");
  ESP_ERROR_CHECK(
      temperature_sensor_install(&temp_sensor_config, &temp_sensor));
  ESP_LOGI(TAG, "Enable temperature sensor");
  ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));

  // ESP_ERROR_CHECK(mesh_light_init());
  ESP_ERROR_CHECK(nvs_flash_init());
  /*  tcpip initialization */
  ESP_ERROR_CHECK(esp_netif_init());
  /*  event initialization */
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  /*  create network interfaces for mesh (only station instance saved for
   * further manipulation, soft AP instance ignored */
  ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
  /*  wifi initialization */
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &ip_event_handler, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
  ESP_ERROR_CHECK(esp_wifi_start());
  /*  mesh initialization */
  ESP_ERROR_CHECK(esp_mesh_init());
  ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID,
                                             &mesh_event_handler, NULL));
  /*  set mesh topology */
  ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
  /*  set mesh max layer according to the topology */
  ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
  ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
  ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
#ifdef CONFIG_MESH_ENABLE_PS
  /* Enable mesh PS function */
  ESP_ERROR_CHECK(esp_mesh_enable_ps());
  /* better to increase the associate expired time, if a small duty cycle is
   * set. */
  ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
  /* better to increase the announce interval to avoid too much management
   * traffic, if a small duty cycle is set. */
  ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
  /* Disable mesh PS function */
  ESP_ERROR_CHECK(esp_mesh_disable_ps());
  ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
#endif
  mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
  /* mesh ID */
  memcpy((uint8_t*)&cfg.mesh_id, MESH_ID, 6);
  /* router */
  cfg.channel = CONFIG_MESH_CHANNEL;
  cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
  memcpy((uint8_t*)&cfg.router.ssid, CONFIG_MESH_ROUTER_SSID,
         cfg.router.ssid_len);
  memcpy((uint8_t*)&cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
         strlen(CONFIG_MESH_ROUTER_PASSWD));
  /* mesh softAP */
  // ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
  ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(WIFI_AUTH_WPA2_PSK));
  cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
  cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
  memcpy((uint8_t*)&cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
         strlen(CONFIG_MESH_AP_PASSWD));
  ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
  /* mesh start */
  ESP_ERROR_CHECK(esp_mesh_start());
#ifdef CONFIG_MESH_ENABLE_PS
  /* set the device active duty cycle. (default:10, MESH_PS_DEVICE_DUTY_REQUEST)
   */
  ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY,
                                                 CONFIG_MESH_PS_DEV_DUTY_TYPE));
  /* set the network active duty cycle. (default:10, -1,
   * MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
  ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(
      CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION,
      CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif
  ESP_LOGI(
      MESH_TAG, "mesh starts successfully, heap:%" PRId32 ", %s<%d>%s, ps:%d",
      esp_get_minimum_free_heap_size(),
      esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
      esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)" : "(tree)",
      esp_mesh_is_ps_enabled());
}
