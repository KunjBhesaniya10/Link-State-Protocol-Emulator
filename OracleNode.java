import java.net.*;
import java.io.*;

public class OracleNode {
   public static void main(){
        // make tcp connection with destination node
        int port = 5000;
        // String ipAddr = "10.51.11.2"
        try(ServerSocket serverSocket = new ServerSocket(port)){
            System.out.println("IP address of Oracle Node: " + InetAddress.getLocalHost().getHostAddress());
            Socket s1 = serverSocket.accept();
            OutputStream s1out = s1.getOutputStream();
            DataOutputStream dos = new DataOutputStream(s1out);

            dos.writeUTF("Hello there !");
            dos.close();
            s1out.close();
            s1.close();
        }
        catch(Exception e){
            e.printStackTrace();
        }
   } 
}
