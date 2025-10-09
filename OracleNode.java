import java.net.*;
import java.util.Scanner;
import java.io.*;

public class OracleNode {
   public static void main(String[] args) {
        // make tcp connection with destination node
        int port = 8080;
        // String ipAddr = "10.51.11.2"
        try(ServerSocket serverSocket = new ServerSocket(port)){
           try( Socket s1 = serverSocket.accept()){
               InputStream s1in = s1.getInputStream();
               OutputStream s1out = s1.getOutputStream();

                byte[] buf = new byte[1024];
                int bytes_read = s1in.read(buf);
                if(bytes_read > 0){
                    String st = new String(buf,0,bytes_read);
                    System.out.println("Message received: ");
                    System.out.println(st);
                }
                String msg = "Hello there";
                s1out.write(msg.getBytes());
                
                scanner.close();
                s1out.close();
                s1.close();
           }
           catch(Exception e){
            e.printStackTrace();
           }
            
        }
        catch(Exception e){
            e.printStackTrace();
        }
   } 
}
