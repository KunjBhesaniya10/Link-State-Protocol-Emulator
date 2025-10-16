import java.net.*;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.util.Scanner;
import java.util.concurrent.ScheduledExecutorService;
import java.io.*;
import java.util.HashMap;
import java.util.List;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;

public class OracleNode {
   
   record Edge(Character target, int weight){}
    
   HashMap<Character,SocketChannel> clientChannels; // list of connected clients
   HashMap<SocketChannel,Character> channelToClientId;
   HashMap<Character,List<Edge>> AdjacencyList; // adjacency list for the graph
   HashMap<Character,List<byte[]>> ipPortInfo; // to store ip and port info of clients
   String configFile = "config.txt";
   long lastModified = 0;
   Selector selector;
   ServerSocket serverSocket;
   ServerSocketChannel serverChannel;
   int port;
   char newClientId = 'A';
   int connectedClients;
   int totalClients;
   Boolean FirstTime = true;

   private static volatile boolean running = true;  

   OracleNode(int port){
        this.port = port;
        clientChannels = new HashMap<>();
        channelToClientId = new HashMap<>();
        AdjacencyList = new HashMap<>();
        ipPortInfo = new HashMap<>();
        connectedClients = 0;
        monitorConfigFile();
        printAdjacencyList();
        totalClients = AdjacencyList.size();
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
   
   void cleanup(){
    // close all channels and selector
    try{
        for(SocketChannel clientChannel : clientChannels.values()){
            clientChannel.close();
        }
        serverChannel.close();
        selector.close();
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
        channelToClientId.put(clientChannel, newClientId);
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

    Boolean monitorConfigFile(){
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
                    int j=0;
                    while(j < parts.length && parts[j].isEmpty()){
                        j++;
                    }
                    if(node == 'A'){
                        for(char c = 'A'; c < (char)('A' + parts.length+1-j); c++){
                            AdjacencyList.put(c, new java.util.ArrayList<>());
                        }
                    }
                    char tmp = (char)(node.charValue() + 1);
                    while(j < parts.length){
                        // System.err.println("Adding edge from " + node + " to " + tmp + " with weight " + parts[j]);
                        if(Integer.parseInt(parts[j])  == -1){
                            j++;
                            tmp++;
                            continue;
                        }
                        AdjacencyList.get(node).add(new Edge(Character.valueOf(tmp), Integer.parseInt(parts[j])));
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
            int nodeCount = 0;

        }
        return false;
    }

    int addIpPortToMessage(byte[] message, int index, Character clientId){
        try{
            List<byte[]> ownIpPort = ipPortInfo.get(clientId);
            if(ownIpPort != null && ownIpPort.size() == 2){
                byte[] ownIpAddr = ownIpPort.get(0);
                byte[] ownPortBytes = ownIpPort.get(1);
                System.arraycopy(ownIpAddr, 0, message, index, 4);
                index += 4;
                System.arraycopy(ownPortBytes, 0, message, index, 2);
                index += 2;
            }
            else{
                IOException e = new IOException("IP/Port info not found for client " + clientId);
                throw e;
            }
        }
        catch(IOException e){
            e.printStackTrace();
        }
        return index;
    }

    void sendMessageToAllClients(){
        for(Character clientId : clientChannels.keySet()){
            if(clientId-'A'+1 > totalClients){
                continue;
            }
            SocketChannel clientChannel = clientChannels.get(clientId);
            List<Edge> edges = AdjacencyList.get(clientId);
            byte[] message = new byte[256];
            int index = 0;
    
            for(Edge edge : edges){
                message[index++] = (byte) edge.target.charValue();
                index = addIpPortToMessage(message, index, edge.target);
                byte[] weightBytes = ByteBuffer.allocate(4).putInt(edge.weight).array();
                for(int k=0; k<4; k++){
                    message[index++] = weightBytes[k];
                }
            }         
    
             
            // add own info at the end
            message[index++] = (byte) clientId.charValue();
            index = addIpPortToMessage(message, index, clientId);                
            for(int k=0; k<4; k++){
                message[index++] = 0; // weight 0 for self
            }
            
            try{
                ByteBuffer buffer = ByteBuffer.wrap(message, 0, index);
                clientChannel.write(buffer);
                System.err.println("Sent message to client " + clientId + ": " + java.util.Arrays.toString(java.util.Arrays.copyOfRange(message, 0, index)));
            }
            catch(IOException e){
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

   void turnOn(){
    
    Runtime.getRuntime().addShutdownHook(
        new Thread(() -> {
            System.out.println("Shutting down Oracle Node...");
            running = false;
            cleanup();
            System.out.println("Oracle Node shut down gracefully.");
        }
    ));


    long lastTime = System.currentTimeMillis();

    while(running){
        // System.err.println("waiting for new connections...");
        try{
            int keys = selector.selectNow();
           
            if(connectedClients >= totalClients){
                // System.err.println("All clients connected. Monitoring config file for changes...");
                // periodically check for config file changes every 30 seconds
                long currentTime = System.currentTimeMillis();
                if(FirstTime){
                    System.err.println("First time sending messages to all clients.");
                    sendMessageToAllClients();
                    FirstTime = false;
                }else if(currentTime - lastTime >= 30000){
                    if(monitorConfigFile()){
                        sendMessageToAllClients();
                    }
                    lastTime = currentTime;
                }
            }

            var selectedKeys = selector.selectedKeys();
            var iter = selectedKeys.iterator();
            while(iter.hasNext()){
                SelectionKey key = iter.next();

                if(key.isAcceptable()){
                    // Handle new connection
                    SocketChannel clientChannel = serverChannel.accept();
                    registerClient(clientChannel);
                }
                if(key.isReadable()){
                    // Handle read
                    SocketChannel clientChannel = (SocketChannel) key.channel();
                    ByteBuffer buffer = ByteBuffer.allocate(256);
                    int bytesRead = clientChannel.read(buffer);
                    System.err.println("Bytes read: " + bytesRead);
                    if(bytesRead < 0){
                        // client has closed the connection
                        Character clientId = channelToClientId.get(clientChannel);
                        System.err.println("Client " + clientId + " has disconnected.");
                        clientChannels.remove(clientId);
                        channelToClientId.remove(clientChannel);
                        AdjacencyList.remove(clientId);
                        ipPortInfo.remove(clientId);
                        connectedClients--;
                        key.cancel();
                        clientChannel.close();
                    }
                    else if(bytesRead == 0){
                        // no data read
                        System.err.println("No data read from client.");
                    }
                    if(bytesRead > 0){
                        byte[] receivedMessage = buffer.array();
                        for(int i=0; i<bytesRead; i++){
                            System.err.print(String.format("%02X ", receivedMessage[i]));
                        }
                        byte[] ipAddr = new byte[4];
                        System.arraycopy(receivedMessage, 0, ipAddr, 0, 4);
                        byte[] portBytes = new byte[2];
                        System.arraycopy(receivedMessage, 4, portBytes, 0, 2);
                        System.err.println("Received IP: " + (ipAddr[0] & 0xFF) + "." + (ipAddr[1] & 0xFF) + "." + (ipAddr[2] & 0xFF) + "." + (ipAddr[3] & 0xFF) + " Port: " + ((portBytes[0] & 0xFF) << 8 | (portBytes[1] & 0xFF)) + " from client " + channelToClientId.get(clientChannel));
                        ipPortInfo.put(channelToClientId.get(clientChannel),List.of(ipAddr, portBytes));
                        connectedClients++;
                    }
                }
                iter.remove();
            }
        }
        catch(IOException e){
            e.printStackTrace();
        }
    }

    cleanup();
    System.out.println("Oracle Node shut down gracefully.");
   }

};

class Main{
    public static void main(String[] args) {
        
        Scanner scanner = new Scanner(System.in);
        System.out.print("Enter port number for Oracle Node: ");
        int port = scanner.nextInt();
        OracleNode oracle = new OracleNode(port);
        oracle.turnOn();
        // oracle.monitorConfigFile();
        // oracle.printAdjacencyList();
        scanner.close();

        // further implementation to handle client connections and queries

    }
}
