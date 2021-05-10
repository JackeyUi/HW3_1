#include "math.h"
#include <stdlib.h>
#include "mbed.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "mbed_rpc.h"
#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

#include "uLCD_4DGL.h"
#include "stm32l475e_iot01_accelero.h"

#define PI 3.14159265

uLCD_4DGL uLCD(D1, D0, D2);


double theta;
int GV = 0;
int dGU = 1, dTA = 0;
char src[10];
int xx = 0;
int x = 0;
int n_src = 30;
int y_u;
int ang_cnt = 0;
int ang_tri = 0;
int16_t pXYZ[3] = {0};
int16_t sXYZ[3] = {0};
int16_t aXYZ[3] = {0};

EventQueue qGUI(32 * EVENTS_EVENT_SIZE);
Thread tGUI(osPriorityNormal, 4 * OS_STACK_SIZE);
Thread tANG(osPriorityNormal, 4 * OS_STACK_SIZE);
Thread bt;

// GLOBAL VARIABLES
constexpr int kTensorArenaSize = 20 * 1024;
uint8_t tensor_arena[kTensorArenaSize];
WiFiInterface *wifi;
InterruptIn btn2(USER_BUTTON);
//InterruptIn btn3(SW3);
volatile int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;

const char* topic = "Mbed";

Thread mqtt_thread(osPriorityHigh);
EventQueue mqtt_queue;

void doGUI(Arguments *in, Reply *out);
void doANG(Arguments *in, Reply *out);
RPCFunction rpcGUI(&doGUI, "doGUI");
RPCFunction rpcANG(&doANG, "doANG");
BufferedSerial pc(USBTX, USBRX);


void flip1() {
    dGU = !dGU;
}
void flip2() {
    dTA = !dTA;
}
void ang_info() {
   if(!ang_tri)  {
     ang_tri = 1;
     sprintf(src, "%lf", theta);
   }
}


int PredictGesture(float* output) {
  // How many times the most recent gesture has been matched in a row
  static int continuous_count = 0;
  // The result of the last prediction
  static int last_predict = -1;

  // Find whichever output has a probability > 0.8 (they sum to 1)
  int this_predict = -1;
  for (int i = 0; i < label_num; i++) {
    if (output[i] > 0.8) this_predict = i;
  }

  // No gesture was detected above the threshold
  if (this_predict == -1) {
    continuous_count = 0;
    last_predict = label_num;
    return label_num;
  }

  if (last_predict == this_predict) {
    continuous_count += 1;
  } else {
    continuous_count = 0;
  }
  last_predict = this_predict;

  // If we haven't yet had enough consecutive matches for this gesture,
  // report a negative result
  if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
    return label_num;
  }
  // Otherwise, we've seen a positive result, so clear all our variables
  // and report it
  continuous_count = 0;
  last_predict = -1;

  return this_predict;
}


void messageArrived(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char msg[300];
    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf(msg);
    ThisThread::sleep_for(1000ms);
    char payload[300];
    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    printf(payload);
    ++arrivedcount;
}

void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client) {
    message_num++;
    MQTT::Message message;
    char buff[100];
    sprintf(buff, "%s", src);
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*) buff;
    message.payloadlen = strlen(buff) + 1;
    int rc = client->publish(topic, message);

    printf("rc:  %d\r\n", rc);
    printf("Puslish message: %s\r\n", buff);
}

void close_mqtt() {
    closed = true;
}

int main() {
 
  uLCD.text_width(2); //4X size text
  uLCD.text_height(2);
  uLCD.locate(1,1);
  uLCD.color(RED);
  printf("Set up uLCD.\r\n");
  uLCD.printf("30\n 45\n 60");
    wifi = WiFiInterface::get_default_instance();
    if (!wifi) {
            printf("ERROR: No WiFiInterface found.\r\n");
            return -1;
    }


    printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
    int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
    if (ret != 0) {
            printf("\nConnection error: %d\r\n", ret);
            return -1;
    }


    NetworkInterface* net = wifi;
    MQTTNetwork mqttNetwork(net);
    MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);

    //TODO: revise host to your IP
    const char* host = "192.168.43.71";
    printf("Connecting to TCP network...\r\n");

    SocketAddress sockAddr;
    sockAddr.set_ip_address(host);
    sockAddr.set_port(1883);

    printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting

    int rc = mqttNetwork.connect(sockAddr);//(host, 1883);
    if (rc != 0) {
            printf("Connection error.");
            return -1;
    }
    printf("Successfully connected!\r\n");

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = "Mbed";

    if ((rc = client.connect(data)) != 0){
            printf("Fail to connect MQTT\r\n");
    }
    if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0){
            printf("Fail to subscribe\r\n");

    }

        char buf[256], outbuf[256];

   FILE *devin = fdopen(&pc, "r");
   FILE *devout = fdopen(&pc, "w");

  mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));
    
   while(1) {
     btn2.rise(mqtt_queue.event(&publish_message, &client));
     while(ang_tri) {
       mqtt_queue.call(&publish_message, &client);
       ang_tri = 0;
     }
     if(!dTA){
      memset(buf, 0, 256);
      for (int i = 0; ; i++) {
          char recv = fgetc(devin);
          if (recv == '\n') {
              printf("\r\n");
              break;
          }
          buf[i] = fputc(recv, devout);
      }
      RPC::call(buf, outbuf);
      printf("%s\r\n", outbuf);
     }
   }


    //btn3.rise(&close_mqtt);

    return 0;
}

void GUIM() {
  dGU = 1;
  // Whether we should clear the buffer next time we fetch data
  bool should_clear_buffer = false;
  bool got_data = false;

  // The gesture index of the prediction
  int gesture_index;

  // Set up logging.
  static tflite::MicroErrorReporter micro_error_reporter;
  tflite::ErrorReporter* error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);


static tflite::MicroOpResolver<6> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(), 1);

  // Build an interpreter to run the model with
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  tflite::MicroInterpreter* interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();

  // Obtain pointer to the model's input tensor
  TfLiteTensor* model_input = interpreter->input(0);

  int input_length = model_input->bytes / sizeof(float);

  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);

  error_reporter->Report("Set up successful...\n\r");

 
 while (dGU) {

    // Attempt to read new data from the accelerometer
    got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                 input_length, should_clear_buffer);

    // If there was no new data,
    // don't try to clear the buffer again and wait until next time
    if (!got_data) {
      should_clear_buffer = false;
      continue;
    }

    // Run inference, and report any error
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
      error_reporter->Report("Invoke failed on index: %d\n", begin_index);
      continue;
    }

    // Analyze the results to obtain a prediction
    gesture_index = PredictGesture(interpreter->output(0)->data.f);

    // Clear the buffer next time we read data
    should_clear_buffer = gesture_index < label_num;
    // Produce an output
    if (gesture_index < label_num) {
      strcpy(src,config.output_message[gesture_index]);
      //error_reporter->Report(config.output_message[gesture_index]);
      printf("%s\n\r", src);
      uLCD.locate(3,y_u);
      uLCD.printf("   ");
      //printf("%d\n\r", dGU);
      n_src = atoi(src);
      y_u = n_src/15 - 1;
      uLCD.locate(3,y_u);
      uLCD.printf("<-");
    }
  }
   printf("stopping GUI mode\n");
}

void ANGM() {
   
   printf("Start accelerometer init\n");
   BSP_ACCELERO_Init();
   printf("Please place the mbed on table stationaryly for 5s\n");
   for(int n = 5; n >= 1; n--) {
     printf("%d\n", n);
     ThisThread::sleep_for(1s);
   }
   BSP_ACCELERO_AccGetXYZ(sXYZ);
   printf("The reference vector is (%d, %d, %d)\n", sXYZ[0], sXYZ[1], sXYZ[2]);
   while(dTA) {
     BSP_ACCELERO_AccGetXYZ(pXYZ);
     aXYZ[0] = pXYZ[0] - sXYZ[0];
     aXYZ[1] = pXYZ[1] - sXYZ[1];
     aXYZ[2] = pXYZ[2] - sXYZ[2];
     printf("The vector is (%d, %d, %d)\n", aXYZ[0], aXYZ[1], aXYZ[2]);
     float ax = -(float) aXYZ[0];
     float sz = (float) sXYZ[2];
     theta = asin(ax/sz) * 180 / PI;
     printf("%lf\n", theta);
     if(theta >= n_src) {
          ang_cnt++;
          ang_info();
     }
     ThisThread::sleep_for(500ms);
   }

}

void doGUI(Arguments *in, Reply *out) {
    x = in->getArg<int>();
    if(x) {
      //flip1();
      tGUI.start(GUIM);
    }
    else 
    flip1();
}

void doANG(Arguments *in, Reply *out) {
    x = in->getArg<int>();
    if(x){
      flip2();
      tANG.start(ANGM);
    }
    else 
    flip2();
}