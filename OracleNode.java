import java.net.*;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.util.Scanner;
import java.io.*;
import java.util.HashMap;
import java.util.List;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;

public class OracleNode {
   
   record Edge(Character target, int weight){}
    
   HashMap<Character,SocketChannel> clientChannels; // list of connected clients
   HashMap<Character,List<Edge>> AdjacencyList; // adjacency list for the graph
   String configFile = "config.txt";
   long lastModified = 0;
   Selector selector;
   ServerSocket serverSocket;
   ServerSocketChannel serverChannel;
   int port;
   char newClientId = 'A';

   OracleNode(int port){
        this.port = port;
        clientChannels = new HashMap<>();
        AdjacencyList = new HashMap<>();
        try{
            this.selector = Selector.open();
            this.serverChannel = ServerSocketChannel.open();
            this.serverChannel.bind(new InetSocketAddress(port));
            this.serverChannel.configureBlocking(false);
            this.serverChannel.register(selector, SelectionKey.OP_ACCEPT);
            this.serverSocket = serverChannel.socket();
        }
        catch(IOException e){
            e.printStackTrace();
        }
    }
   
   void registerClient(SocketChannel clientChannel){
    try{
        clientChannel.configureBlocking(false);
        clientChannel.register(selector, SelectionKey.OP_READ);
        clientChannels.put(newClientId, clientChannel);
        System.err.println("Registered new client with ID: " + newClientId);
        newClientId++;
    }
    catch(IOException e){
        e.printStackTrace();
    }
   }
    // void sendMessage(SocketChannel clientChannel, String receivedMessage){
    //  try{
    //     // send list of [Node,ip,port,cost] (of neighbors) to client
    //     // receivedMessage has ip and port of client. (32 bits and 16 bits)

    //  }
    // }

   void monitorConfigFile(){
    // monitor the config file for changes and update the adjacency list
    File file = new File(configFile);
    if(file.lastModified() > lastModified){
        lastModified = file.lastModified();
        try(BufferedReader br = new BufferedReader(new FileReader(file))){
            String line;
            Character node = 'A';
            while((line = br.readLine()) != null){
                line = line.trim();
                if(line.isEmpty() || line.startsWith("#")){
                    continue; // skip empty lines and comments
                }
                String[] parts = line.split("\\s+");
                // System.err.println(parts.length + " parts found in line: " + line);
                int j=0;
                while(j < parts.length && parts[j].isEmpty()){
                    j++;
                }
                AdjacencyList.put(node, new java.util.ArrayList<>());
                char tmp = (char)(node.charValue() + 1);
                while(j < parts.length){
                    // System.err.println("Adding edge from " + node + " to " + tmp + " with weight " + parts[j]);
                    if(Integer.parseInt(parts[j])  == -1){
                        j++;
                        tmp++;
                        continue;
                    }
                    AdjacencyList.get(node).add(new Edge(Character.valueOf(tmp), Integer.parseInt(parts[j])));
                    AdjacencyList.putIfAbsent(tmp, new java.util.ArrayList<>());
                    AdjacencyList.get(tmp).add(new Edge(Character.valueOf(node), Integer.parseInt(parts[j])));
                    j++;
                    tmp++;
                }
                node = (char)(node.charValue() + 1);
            }
        }
        catch(Exception e){
            e.printStackTrace();
        }
    }

   }

   void printAdjacencyList(){
    for(Character node : AdjacencyList.keySet()){
        System.out.print(node + ": ");
        for(Edge edge : AdjacencyList.get(node)){
            System.out.print(" -> " + edge.target + "(" + edge.weight + ")");
        }
        System.out.println();
    }
   }



};

class Main{
    public static void main(String[] args) {
        
        Scanner scanner = new Scanner(System.in);
        System.out.print("Enter port number for Oracle Node: ");
        int port = scanner.nextInt();
        OracleNode oracle = new OracleNode(port);
        oracle.monitorConfigFile();
        oracle.printAdjacencyList();
        
        // further implementation to handle client connections and queries
        
        while(true){
            // System.err.println("waiting for new connections...");
            try{
                int keys = oracle.selector.selectNow();
                if(keys == 0){
                    // check timer and monitor config file
                    // System.err.println("no keys, checking config file...");
                    continue;
                }
                var selectedKeys = oracle.selector.selectedKeys();
                var iter = selectedKeys.iterator();
                while(iter.hasNext()){
                    SelectionKey key = iter.next();

                    if(key.isAcceptable()){
                        // Handle new connection

                        SocketChannel clientChannel = oracle.serverChannel.accept();
                        // if(clientChannel == null){
                        //     continue;
                        // }
                        oracle.registerClient(clientChannel);
                    }
                    else if(key.isReadable()){
                        // Handle read
                        SocketChannel clientChannel = (SocketChannel) key.channel();
                        ByteBuffer buffer = ByteBuffer.allocate(256);
                        int bytesRead = clientChannel.read(buffer);
                        System.err.println("Bytes read: " + bytesRead);
                        if(bytesRead == -1){
                            // Client has closed the connection
                            clientChannel.close();
                            key.cancel();
                            System.err.println("No bytes read");
                        }
                        else if(bytesRead > 0){
                            byte[] receivedMessage = buffer.array();
                            // send response to client
                            String message = new String(receivedMessage).trim();
                            System.err.println("Received message: " + message);
                            // oracle.sendMessage(clientChannel,message);

                        }
                    }
                    iter.remove();
                }
            }
            catch(IOException e){
                e.printStackTrace();
            }
        }
    }
}
