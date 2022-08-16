import { useGraphQLPagedQuery } from '/common/useGraphQLQuery.mjs';
import { siblingUrl } from '/common/rootdomain.mjs';
import htm from 'https://unpkg.com/htm@3.1.0?module';
const html = htm.bind(React.createElement);

const App = () => {
    const query = `{
        blocks(@page@) {
            pageInfo {
                hasPreviousPage
                hasNextPage
                startCursor
                endCursor
            }
            edges {
                node {
                    header {
                        blockNum
                        previous
                        time
                    }
                }
            }
        }
    }`;
    const pagedResult = useGraphQLPagedQuery('/graphql', query, {
        pageSize: 10,
        getPageInfo: (result) => result.data?.blocks.pageInfo,
        defaultArgs: `last: 10`
    });
    const tdStyle = { border: "1px solid" };
    
    React.useEffect(()=>{
        console.info(`Explorer.useEffect() called; pagedResult.data exists...`);
        const interval=setInterval(()=>{
            console.info("\n========= BEGIN ============")
            console.info("Interval: refreshing data...");
            pagedResult.last();
        },1000)
             
        // return ()=> { // figure out why this is running when it's running
        //     console.info('Explorer.useEffect().dismount');
        //     clearInterval(interval);
        // }
    }, []);

    console.info('rendering...');
    if (!pagedResult.result.data) {
        console.info('no data yet...');
        return html`<div>Loading data...</div>`;
    }
    // console.info('pagedResult:');
    // console.info(pagedResult);
    return html`
        <div class="ui container">
            <a href=${siblingUrl()}>chain</a>
            <h1>explore-sys</h1>
        
            <button onClick=${pagedResult.first}>First</button>
            <button disabled=${!pagedResult.hasPreviousPage} onClick=${pagedResult.previous}>Previous</button>
            <button disabled=${!pagedResult.hasNextPage} onClick=${pagedResult.next}>Next</button>
            <button onClick=${pagedResult.last}>Last</button>
            <table>
                <tbody>
                    <tr>
                        <th style=${tdStyle}>Block</th>
                        <th style=${tdStyle}>Previous</th>
                        <th style=${tdStyle}>Time</th>
                    </tr>
                    ${pagedResult.result.data?.blocks.edges.map?.(e => html`<tr>
                        <td style=${tdStyle}>${e.node.header.blockNum}</td>
                        <td style=${tdStyle}>
                            <pre>${e.node.header.previous}</pre>
                        </td>
                        <td style=${tdStyle}>${e.node.header.time}</td>
                    </tr>`)}
                </tbody>
            </table>
        </div>`;
};

const container = document.getElementById('root');
const root = ReactDOM.createRoot(container);
root.render(html`<${App} />`);
