import SwiftUI

struct ContentView: View {
    var body: some View {
        TabView {
            
            // 1) SYNC TAB
            SyncView() // or your existing sync code inline
                .tabItem {
                    Label("Sync", systemImage: "arrow.2.circlepath")
                }
            
            // 2) METRICS TAB
            MetricsView()
                .tabItem {
                    Label("Metrics", systemImage: "chart.bar")
                }
            
            // 3) FAQ TAB
            FAQView()
                .tabItem {
                    Label("FAQ", systemImage: "questionmark.circle")
                }
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
