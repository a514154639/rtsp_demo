
#include <opencv2/core/types_c.h>
#include <opencv2/highgui.hpp>
#include <time.h>
#include <string>
#include <iostream>
#include <gst/gst.h>

using namespace cv;
using namespace std;


/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
    GstElement *pipeline;
    GstElement *source;
    GstElement *depay;
    GstElement *queue;    
    GstElement *parse;
    GstElement *avdec;
    GstElement *conv;
    GstElement *encoder;
    GstElement *convert;
    GstElement *sink;
    GMainLoop *main_loop;  /* GLib's Main Loop */


    // GstElement *pipeline, *source, *depay, *parse, *avdec, *convert, *encoder, *sink;
} CustomData;

/* This function is called when an error message is posted on the bus */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;

  /* Print error details on the screen */
  gst_message_parse_error (msg, &err, &debug_info);
  g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);

  g_main_loop_quit (data->main_loop);
}



/* Handler for the pad-added signal */
/* This function will be called by the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
    GstPad *sink_pad = gst_element_get_static_pad (data->depay, "sink");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;
 
    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
 
    /* If our converter is already linked, we have nothing to do here */
    if (gst_pad_is_linked (sink_pad)) {
        g_print ("We are already linked. Ignoring.\n");
        goto exit;
    }
 
    /* Check the new pad's type */
    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);
 
    /* Attempt the link */
    ret = gst_pad_link (new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) {
        g_print ("Type is '%s' but link failed.\n", new_pad_type);
    } else {
        g_print ("Link succeeded (type '%s').\n", new_pad_type);
    }
 
exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);
 
    /* Unreference the sink pad */
    gst_object_unref (sink_pad);
} 


static GstFlowReturn new_sample (GstElement *sink, gpointer user_data){
 
        GstSample *sample;
        GstBuffer *buffer;
        GstCaps *caps;
        GstStructure *s;
        gint width, height;	//图片的尺寸
 
        /* Retrieve the buffer */
        g_signal_emit_by_name (sink, "pull-sample", &sample);
        if (sample){
            g_print ("*  ");
            caps = gst_sample_get_caps (sample);
            if (!caps) {
                g_print ("gst_sample_get_caps fail\n");
                gst_sample_unref (sample);
                return GST_FLOW_ERROR;
            }
            s = gst_caps_get_structure (caps, 0);
            gboolean res;
            res = gst_structure_get_int (s, "width", &width);		//获取图片的宽

            g_print ("width: %d,  ", width);
            res |= gst_structure_get_int (s, "height", &height);	//获取图片的高
            g_print ("height: %d \n", height);

            if (!res) {
                g_print ("gst_structure_get_int fail\n");
                gst_sample_unref (sample);
                return GST_FLOW_ERROR;
            }
 
            //获取视频的一帧buffer,注意，这个buffer是无法直接用的，它不是char类型
            buffer = gst_sample_get_buffer (sample);		
 
            if(!buffer){
                g_print ("gst_sample_get_buffer fail\n");
                gst_sample_unref (sample);
                return GST_FLOW_ERROR;
            }
             
        
            GstMapInfo map;
            //把buffer映射到map，这样我们就可以通过map.data取到buffer的数据
            if (gst_buffer_map (buffer, &map, GST_MAP_READ)){	                   	
                    g_print("jpg size = %ld \n", map.size);                   
                    cv::Mat src(Size(width, height), CV_8UC3, (char*)map.data, Mat::AUTO_STEP);
                    //文件名  以获取当前时间作为文件命名
                    time_t timep;
                    time(&timep); 
                    printf("%s", ctime(&timep));   
                    string ss = ".jpg";
                    string filename =  ctime(&timep) + ss ;           
                    cv::imwrite("./out/" + filename, src);    
                    gst_buffer_unmap (buffer, &map);	//解除映射
                }
            gst_sample_unref (sample);	//释放资源
            return GST_FLOW_OK;
        }
    return GST_FLOW_OK ;
 
}


#define CAPS "video/x-raw,format=BGR"	//设置appsink输出的视频格式


int main(int argc, char *argv[]) {
    /* Configure appsink */
    CustomData data;
    GstBus *bus;
    GstMessage *msg;
    GstStateChangeReturn ret;
    gboolean terminate = FALSE;
 
    /* Initialize GStreamer */
    gst_init (&argc, &argv);
    g_printerr("gst_init \n");


    /* Create the elements 
      videotestsrc 是一个源元素（它生成数据），用于创建测试视频模式。
      此元素对于调试目的（和教程）非常有用，通常在实际应用程序中找不到。
      autovideosink是一个接收器元素（它消耗数据），在窗口上显示它接收的图像。
    */
    data.source = gst_element_factory_make ("rtspsrc", "source");
    g_object_set (G_OBJECT (data.source), "latency", 2000, NULL);
    data.depay = gst_element_factory_make ("rtph264depay", "depay");
    data.queue = gst_element_factory_make ("queue", "Queue");

    data.parse = gst_element_factory_make ("h264parse", "parse");
    data.avdec = gst_element_factory_make ("nvv4l2decoder", "decoder");
    data.conv = gst_element_factory_make ("nvvidconv", "convert");

    data.encoder = gst_element_factory_make("nvv4l2h264enc", "encoder");     // nv4l2h264enc、nv4l2vp9enc和nvjpegenc,omxh264enc,jpegenc

    data.convert = gst_element_factory_make("videoconvert","Videoconvert");
    data.sink = gst_element_factory_make ("appsink", "sink");   
    g_printerr("gst element factory make \n");

    
    GstCaps *video_caps;
    gchar *video_caps_text;
    video_caps_text = g_strdup_printf (CAPS);
    video_caps = gst_caps_from_string (video_caps_text);

    if(!video_caps){
    g_printerr("gst_caps_from_string fail\n");
    return -1;
    }




    g_object_set (data.source, "location", "rtsp://admin:chd@10.100.8.64:554/0", NULL);
    // 因为rtsp和rtph264depay还没有连接，所以必须设置pad-added信号监听，在管道开始工作后，确定了数据格式，再把它们连接起来
    /* Connect to the pad-added signal */
    g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);

    g_object_set (data.sink, "caps", video_caps, NULL);

    g_object_set (data.sink, "emit-signals", TRUE, NULL);
    g_signal_connect (data.sink, "new-sample", G_CALLBACK (new_sample), &data);
    g_printerr("gst signal connect \n");

    data.pipeline = gst_pipeline_new ("test-pipeline");



    //&& 如果两个操作数都非零，则条件为真；
    // || 如果两个操作数中有任意一个非零，则条件为真。
    if (!data.pipeline || !data.source ||!data.depay || !data.queue  ||!data.parse || !data.avdec || !data.conv  || !data.encoder ||!data.convert || !data.sink) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    }
 
    /* Build the pipeline. Note that we are NOT linking the source at this
    * point. We will do it later. */
    gst_bin_add_many (GST_BIN (data.pipeline), data.source, data.depay, data.queue, data.parse, data.avdec, data.conv,  data.convert,data.sink, NULL);



    if (!gst_element_link_many ( data.depay,data.queue,data.parse, data.avdec,data.conv,  data.convert,data.sink,NULL)) {
        g_printerr ("Elements could not be linked.\n");
        gst_object_unref (data.pipeline);
        return -1;
    }



    /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
    bus = gst_element_get_bus (data.pipeline);
    gst_bus_add_signal_watch (bus);
    g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
    gst_object_unref (bus);



    /* Start playing */
    ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set the pipeline to the playing state.\n");
        gst_object_unref (data.pipeline);
        return -1;
    }
 

    /* Create a GLib Main Loop and set it to run */
    data.main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (data.main_loop);


    
 
    /* Free resources */
    gst_object_unref (bus);
    gst_element_set_state (data.pipeline, GST_STATE_NULL);
    gst_object_unref (data.pipeline);
    return 0;
}    
