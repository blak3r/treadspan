import SwiftUI

struct FAQItem: Identifiable {
    let id = UUID()
    let question: String
    let answer: AnyView // Change Text to AnyView for flexibility
}

struct FAQView: View {
    @State private var expandedQuestion: UUID?

    let faqItems: [FAQItem] = [
        FAQItem(
            question: "Is this app affiliated with LifeSpan Fitness?",
            answer: AnyView(
                Text("No. This app is in no way associated with LifeSpan Fitness. It was built by Blake Robertson (a LifeSpan customer).")
            )
        ),
        FAQItem(
            question: "Do I need anything else beside the app for it to work?",
            answer: AnyView(
                HStack {
                    Text("Yes, in order to log multiple sessions you need to buy a ~$17 module.")
                }
            )
        ),
        FAQItem(
            question: "How do I submit feature requests / support?",
            answer: AnyView(
                HStack {
                    Text("Go to ")
                    Link("", destination: URL(string: "https://github.com/blak3r/treadspan/discussions")!)
                        .foregroundColor(.blue)
                        .underline()
                }
            )
        ),
        FAQItem(
            question: "Why do you need READ steps permission?",
            answer: AnyView(
                HStack {
                    Text("Read permission is needed for the metrics view to work.  Our metrics view is very similar to the Apple Health Steps graphs but adds a data series that shows how many steps you take on a treadmill.  To do this we need to read the number of steps you got from other data sources as well.")
                }
            )
        ),
        FAQItem(
            question: "Is any information shared with 3rd Parties?",
            answer: AnyView(
                HStack {
                    Text("Absolutely not. The app does report any information to 3rd parties.  All information is stored in Apple HealthKit.")
                }
            )
        ),
        FAQItem(
            question: "Can I still use LifeSpan Fit?",
            answer: AnyView(
                HStack {
                    Text("In order to the Treadspan solution to work there is a chip that connects to your LifeSpan Omni Console.  This chip uses the same Bluetooth connection that the Lifespan Mobile App uses.  As such, as long as the chip is powered on it'll occupy the connection and therefore the original LifeSpan Mobile App will not be able to connect. If you power down the chip by unplugging it's usb-c connector, then you will notice that the bluetooth icon on the console will go grey or disappear.  You will then be able to connect the original LifeSpan app.")
                }
            )
        ),
        FAQItem(
            question: "Which model treadmills are supported?",
            answer: AnyView(
                HStack {
                    Text("Currently, the solution has been tested and developed to work with the ")
                    Link("", destination: URL(string: "https://amzn.to/4bbn8ok")!)
                        .foregroundColor(.blue)
                        .underline()
                    Text("It should work with any LifeSpan treadmill that works with the Omni Console.  If you have the RETRO console, there is a solution that will work as well but the hardware portion requires some assembly. It's probably out of reach if you're not a tinkerer");
                    
                }
            )
        ),
        FAQItem(
            question: "What Treadmills are Supported?",
            answer: Text("Currently, the solution has been tested and developed to work with the ")
            +
                    Text("It should work with any LifeSpan treadmill that works with the Omni Console (TR1000, TR1200, & TR5000).  If you have the RETRO console, there is a solution that will work as well but the hardware portion requires some assembly. It's probably out of reach if you're not a tinkerer")
                    
        ),
        
    ]

    var body: some View {
        NavigationView {
            List {
                ForEach(faqItems) { item in
                    DisclosureGroup(
                        isExpanded: Binding(
                            get: { expandedQuestion == item.id },
                            set: { expandedQuestion = $0 ? item.id : nil }
                        ),
                        content: {
                            item.answer
                                .padding(.top, 5)
                        },
                        label: {
                            Text(item.question)
                                .font(.headline)
                        }
                    )
                }
            }
            .navigationTitle("FAQ")
        }
    }
}

struct FAQView_Previews: PreviewProvider {
    static var previews: some View {
        FAQView()
    }
}
